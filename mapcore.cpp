// WSPR Scheduler Map 3 core - see mapcore.h.
#include "mapcore.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <cmath>
#include <functional>

namespace Map {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
constexpr double kDeg = 180.0 / kPi;
}

// ---- locators ------------------------------------------------------------------------------------

std::optional<LatLon> locatorToLatLon(const QString &locator)
{
    const QString l = locator.trimmed().toUpper();
    static const QRegularExpression re("^[A-R]{2}[0-9]{2}([A-X]{2}([0-9]{2}([A-X]{2})?)?)?$");
    if (!re.match(l).hasMatch())
        return std::nullopt;
    auto c = [&](int i) { return double(l[i].unicode()); };
    LatLon p;
    // the same sums as C# MaidenheadLocator.LocatorToLatLng (the centre of the square)
    switch (l.size()) {
    case 4:
        p.lon = (c(0) - 'A') * 20 + (c(2) - '0' + 0.5) * 2 - 180;
        p.lat = (c(1) - 'A') * 10 + (c(3) - '0' + 0.5) - 90;
        break;
    case 6:
        p.lon = (c(0) - 'A') * 20 + (c(2) - '0') * 2 + (c(4) - 'A' + 0.5) / 12 - 180;
        p.lat = (c(1) - 'A') * 10 + (c(3) - '0') + (c(5) - 'A' + 0.5) / 24 - 90;
        break;
    case 8:
        p.lon = (c(0) - 'A') * 20 + (c(2) - '0') * 2 + (c(4) - 'A') / 12 + (c(6) - '0' + 0.5) / 120 - 180;
        p.lat = (c(1) - 'A') * 10 + (c(3) - '0') + (c(5) - 'A') / 24 + (c(7) - '0' + 0.5) / 240 - 90;
        break;
    default:   // 10
        p.lon = (c(0) - 'A') * 20 + (c(2) - '0') * 2 + (c(4) - 'A') / 12 + (c(6) - '0') / 120
                + (c(8) - 'A' + 0.5) / 120 / 24 - 180;
        p.lat = (c(1) - 'A') * 10 + (c(3) - '0') + (c(5) - 'A') / 24 + (c(7) - '0') / 240
                + (c(9) - 'A' + 0.5) / 240 / 24 - 90;
        break;
    }
    return p;
}

// ---- projection ----------------------------------------------------------------------------------

double worldSize(int zoom)
{
    return double(kTileSize) * double(1 << qBound(0, zoom, 30));
}

QPointF project(double lat, double lon, int zoom)
{
    const double w = worldSize(zoom);
    const double la = qBound(-kMaxLat, lat, kMaxLat) * kRad;
    const double x = (lon + 180.0) / 360.0 * w;
    const double y = (1.0 - std::log(std::tan(la) + 1.0 / std::cos(la)) / kPi) / 2.0 * w;
    return {x, y};
}

LatLon unproject(const QPointF &px, int zoom)
{
    const double w = worldSize(zoom);
    LatLon p;
    p.lon = px.x() / w * 360.0 - 180.0;
    const double n = kPi - 2.0 * kPi * px.y() / w;
    p.lat = kDeg * std::atan(0.5 * (std::exp(n) - std::exp(-n)));
    return p;
}

// ---- paths and night -----------------------------------------------------------------------------

QVector<QPointF> greatCircle(double lat1, double lon1, double lat2, double lon2, int steps)
{
    QVector<QPointF> pts;
    const double p1 = lat1 * kRad, l1 = lon1 * kRad, p2 = lat2 * kRad, l2 = lon2 * kRad;
    const double d = 2 * std::asin(std::sqrt(std::pow(std::sin((p2 - p1) / 2), 2)
                                             + std::cos(p1) * std::cos(p2) * std::pow(std::sin((l2 - l1) / 2), 2)));
    if (d < 1e-9 || !std::isfinite(d))
        return pts;
    steps = qMax(2, steps);
    pts.reserve(steps + 1);
    for (int i = 0; i <= steps; ++i) {
        const double f = double(i) / steps;
        const double a = std::sin((1 - f) * d) / std::sin(d);
        const double b = std::sin(f * d) / std::sin(d);
        const double x = a * std::cos(p1) * std::cos(l1) + b * std::cos(p2) * std::cos(l2);
        const double y = a * std::cos(p1) * std::sin(l1) + b * std::cos(p2) * std::sin(l2);
        const double z = a * std::sin(p1) + b * std::sin(p2);
        double lon = std::atan2(y, x) * kDeg;
        const double lat = std::atan2(z, std::sqrt(x * x + y * y)) * kDeg;
        if (!pts.isEmpty()) {   // unwrap: keep the line continuous across the 180 degree meridian
            const double prev = pts.last().x();
            while (lon - prev > 180) lon -= 360;
            while (prev - lon > 180) lon += 360;
        }
        pts << QPointF(lon, lat);
    }
    return pts;
}

Terminator terminator(const QDateTime &utcIn)
{
    const QDateTime utc = utcIn.toUTC();
    Terminator t;
    const int doy = utc.date().dayOfYear();
    const double b = (360.0 / 365.0) * (doy - 81) * kRad;
    double dec = std::asin(std::sin(23.45 * kRad) * std::sin(b));   // solar declination
    if (std::abs(dec) < 1e-6)
        dec = 1e-6;   // the equinox: keep tan(dec) away from 0
    const QTime tm = utc.time();
    const double utcH = tm.hour() + tm.minute() / 60.0 + tm.second() / 3600.0;
    const double sunLon = (12 - utcH) * 15 * kRad;   // longitude under the sun
    t.line.reserve(721);
    for (int i = 0; i <= 720; ++i) {
        const double lon = -180 + i * 0.5;
        const double h = sunLon - lon * kRad;
        const double lat = std::atan(-std::cos(h) / std::tan(dec)) * kDeg;
        t.line << QPointF(lon, lat);
    }
    t.southDark = dec >= 0;
    return t;
}

// ---- lists ---------------------------------------------------------------------------------------

QStringList periodNames()
{
    return {"10 mins", "20 mins", "30 mins", "1 hour", "2 hours", "3 hours", "6 hours", "9 hours", "12 hours",
            "18 hours", "24 hours", "2 days", "4 days", "7 days", "10 days", "14 days", "21 days", "28 days"};
}

double periodHours(int index)
{
    static const double h[] = {0.16666, 0.33333, 0.5, 1, 2, 3, 6, 9, 12, 18, 24, 48, 96, 168, 240, 336, 504, 672};
    return index >= 0 && index < int(sizeof h / sizeof h[0]) ? h[index] : 0;
}

QDateTime periodStart(const QDateTime &nowUtc, int index)
{
    const double p = periodHours(index);
    const QDateTime now = nowUtc.toUTC();
    if (p <= 0)
        return now;
    if (p < 1)   // 10, 20, 30 minutes
        return now.addSecs(-60 * qint64(std::ceil(p * 60)));
    return now.addSecs(-qint64(p * 3600));
}

QStringList bandNames()
{
    return {"All", "LF", "MF", "160m", "80m", "60m", "40m", "30m", "22m", "20m",
            "17m", "15m", "12m", "10m", "8m", "6m", "4m", "2m", "70cm", "23cm"};
}

QString bandFrequencyPrefix(int index)
{
    static const char *p[] = {"", "0.136", "0.47", "1.8", "3.5", "5.", "7.", "10", "13", "14",
                              "18", "21", "24", "28", "40", "50", "70", "144", "432", "1296"};
    return index > 0 && index < int(sizeof p / sizeof p[0]) ? QString::fromLatin1(p[index]) : QString();
}

int bandNumber(int index)
{
    static const int b[] = {-2, -1, 0, 1, 3, 5, 7, 10, 13, 14, 18, 21, 24, 28, 40, 50, 70, 144, 432, 1296};
    return index >= 0 && index < int(sizeof b / sizeof b[0]) ? b[index] : -2;
}

QString bandLabel(int band)
{
    switch (band) {
    case -1: return "LF";
    case 0: return "MF";
    case 1: return "160";
    case 3: return "80";
    case 5: return "60";
    case 7: return "40";
    case 10: return "30";
    case 13: return "22";
    case 14: return "20";
    case 18: return "17";
    case 21: return "15";
    case 24: return "12";
    case 28: return "10";
    case 40: return "8";
    case 50: return "6";
    case 70: return "4";
    case 144: return "2";
    case 432: return "70cm";
    case 1296: return "23cm";
    default: return "?";
    }
}

QStringList declutterSteps(bool km)
{
    if (km)
        return {"0", "80", "160", "320", "640", "960", "1300", "1600", "1900", "2400", "2900"};
    return {"0", "50", "100", "200", "400", "600", "800", "1000", "1200", "1500", "1800"};
}

double milesToKm(double miles)
{
    return miles * 1.60934;
}

bool specialCall(const QString &call)
{
    if (call.startsWith('0') || call.startsWith('Q') || call.startsWith('1'))
        return true;
    return call.size() > 1 && call[0].isDigit() && call[1].isDigit();
}

// ---- MapData -------------------------------------------------------------------------------------

void MapData::add(bool rxPoint, double lat, double lon, const QString &label)
{
    const QString key = QString::asprintf("%.2f,%.2f", lat, lon);
    QSet<QString> &seen = rxPoint ? m_rxSeen : m_txSeen;
    if (seen.contains(key))
        return;
    seen.insert(key);
    (rxPoint ? rx : tx).append(Point{lat, lon, label});
}

void MapData::clearPoints()
{
    rx.clear();
    tx.clear();
    m_rxSeen.clear();
    m_txSeen.clear();
}

// ---- local databases -----------------------------------------------------------------------------

bool mysqlAnswers(const DbSettings &db)
{
    QTcpSocket s;
    const QString h = db.host.trimmed().isEmpty() || db.host == "localhost" ? QString("127.0.0.1") : db.host;
    s.connectToHost(h, quint16(db.port > 0 ? db.port : 3306));
    const bool ok = s.waitForConnected(300);
    s.abort();
    return ok;
}

namespace {
// a connection for this call only, in whichever thread calls it
template <class F>
bool withDb(const DbSettings &s, const QString &database, QString *err, F &&job)
{
    if (!mysqlAnswers(s)) {
        if (err)
            *err = QString("Can't connect to MySQL server on '%1' (port %2 not answering)").arg(s.host).arg(s.port);
        return false;
    }
    const QString name = "map_" + QUuid::createUuid().toString(QUuid::Id128);
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", name);
        db.setHostName(s.host);
        db.setPort(s.port);
        db.setUserName(s.user);
        db.setPassword(s.password);
        db.setDatabaseName(database);
        db.setConnectOptions("MYSQL_OPT_CONNECT_TIMEOUT=5");
        if (db.open()) {
            ok = true;
            job(db);
        } else if (err) {
            *err = db.lastError().text();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QString utcText(const QDateTime &t)
{
    return t.toUTC().toString("yyyy-MM-dd HH:mm:00");   // C#: "yyyy-MM-dd HH:mm:00"
}

QString bandSuffix(const Request &r, int band)
{
    return r.bandIndex == 0 ? " (" + bandLabel(band) + ")" : QString();   // only when all bands are shown
}

// C#: find_selectedRX - stations this station heard (wspr_rpt.received, from WSPR Scheduler)
bool readReceived(const Request &r, const DbSettings &s, const QDateTime &from, const QDateTime &to, MapData &out,
                  QString *err)
{
    bool ok = true;
    const bool opened = withDb(s, s.rptDatabase, err, [&](QSqlDatabase db) {
        QString sql = "SELECT tx_sign, tx_loc, band FROM received WHERE datetime >= ? AND datetime <= ?";
        QVariantList args{utcText(from), to.toUTC().toString("yyyy-MM-dd HH:mm:ss")};   // to: this second, not this minute
        const QString mhz = bandFrequencyPrefix(r.bandIndex);
        if (!mhz.isEmpty()) {
            sql += " AND frequency LIKE ?";
            args << mhz + "%";
        }
        if (r.minKm > 0) {
            sql += " AND distance >= ?";
            args << r.minKm;
        }
        sql += QString(" ORDER BY datetime DESC LIMIT %1").arg(qMax(1, r.dbRows));
        QSqlQuery q(db);
        q.setForwardOnly(true);
        q.prepare(sql);
        for (int i = 0; i < args.size(); ++i)
            q.bindValue(i, args[i]);
        if (!q.exec()) {
            ok = false;
            if (err)
                *err = q.lastError().text();
            return;
        }
        while (q.next()) {
            const QString sign = q.value(0).toString().trimmed();
            if (sign.isEmpty() || sign == "nil rcvd" || (r.hideSpecial && specialCall(sign)))
                continue;
            if (const auto p = locatorToLatLon(q.value(1).toString()))
                out.add(true, p->lat, p->lon, sign + bandSuffix(r, q.value(2).toInt()));
        }
    });
    return opened && ok;
}

// C#: find_selectedTX - stations that heard this station (wspr_rx.reported, from WSPR Scheduler Live)
bool readReported(const Request &r, const DbSettings &s, const QDateTime &from, const QDateTime &to, MapData &out,
                  QString *err)
{
    bool ok = true;
    const bool opened = withDb(s, s.rxDatabase, err, [&](QSqlDatabase db) {
        QString sql = "SELECT rx_sign, rx_loc, band FROM reported WHERE time >= ? AND time <= ?";
        QVariantList args{utcText(from), to.toUTC().toString("yyyy-MM-dd HH:mm:ss")};   // to: this second, not this minute
        const int band = bandNumber(r.bandIndex);
        if (band != -2) {
            sql += " AND band = ?";
            args << band;
        }
        if (r.minKm > 0) {
            sql += " AND distance >= ?";
            args << r.minKm;
        }
        sql += QString(" ORDER BY time DESC LIMIT %1").arg(qMax(1, r.dbRows));
        QSqlQuery q(db);
        q.setForwardOnly(true);
        q.prepare(sql);
        for (int i = 0; i < args.size(); ++i)
            q.bindValue(i, args[i]);
        if (!q.exec()) {
            ok = false;
            if (err)
                *err = q.lastError().text();
            return;
        }
        while (q.next()) {
            const QString sign = q.value(0).toString().trimmed();
            if (sign.isEmpty() || (r.hideSpecial && specialCall(sign)))
                continue;
            if (const auto p = locatorToLatLon(q.value(1).toString()))
                out.add(false, p->lat, p->lon, sign + bandSuffix(r, q.value(2).toInt()));
        }
    });
    return opened && ok;
}
} // namespace

std::optional<Station> findOwnStation(const DbSettings &s, QString *err)
{
    std::optional<Station> st;
    withDb(s, s.rptDatabase, err, [&](QSqlDatabase db) {
        QSqlQuery q(db);
        if (!q.exec("SELECT reporter, reporter_loc FROM received ORDER BY datetime DESC LIMIT 5")) {
            if (err)
                *err = q.lastError().text();
            return;
        }
        Station found;
        while (q.next()) {
            if (found.call.isEmpty())
                found.call = q.value(0).toString().trimmed();
            if (found.locator.isEmpty())
                found.locator = q.value(1).toString().trimmed();
            if (!found.call.isEmpty() && !found.locator.isEmpty()) {
                st = found;
                return;
            }
        }
        if (err && err->isEmpty())
            *err = "no reports in wspr_rpt.received yet";
    });
    return st;
}

void ensureIndexes(const DbSettings &s)
{
    // MariaDB has CREATE INDEX IF NOT EXISTS; MySQL 8 doesn't, so look first
    auto ensure = [](QSqlDatabase db, const QString &table, const QString &name, const QString &cols) {
        QSqlQuery q(db);
        q.prepare("SELECT 1 FROM information_schema.statistics WHERE table_schema = DATABASE() "
                  "AND table_name = ? AND index_name = ? LIMIT 1");
        q.bindValue(0, table);
        q.bindValue(1, name);
        if (q.exec() && q.next())
            return;
        q.exec(QString("CREATE INDEX %1 ON %2 %3").arg(name, table, cols));   // an optimisation: errors ignored
    };
    withDb(s, s.rptDatabase, nullptr, [&](QSqlDatabase db) {
        ensure(db, "received", "idx_received_datetime", "(datetime)");
        ensure(db, "received", "idx_received_frequency", "(frequency)");
    });
    withDb(s, s.rxDatabase, nullptr, [&](QSqlDatabase db) {
        ensure(db, "reported", "idx_reported_time", "(time)");
        ensure(db, "reported", "idx_reported_band", "(band)");
    });
}

// ---- wspr.live -----------------------------------------------------------------------------------

namespace {
const char *kLiveColumns = "band, rx_sign, rx_lat, rx_lon, rx_loc, tx_sign, tx_lat, tx_lon, tx_loc, distance";
}

bool validCall(const QString &call)
{
    static const QRegularExpression re("^[A-Z0-9/]{2,15}$");
    return re.match(call).hasMatch();
}

QString liveQuery(const QString &signColumn, const QString &call, int minutes, int limit)
{
    // C#: SELECT * ... LIKE '%call%' (so G4GCI also finds G4GCI/P); the newest first, so the limit
    // drops the oldest spots and not random ones
    return QString("SELECT %1 FROM wspr.rx WHERE %2 LIKE '%%3%' AND time >= subtractMinutes(now(), %4) "
                   "AND time <= subtractMinutes(now(), 2) ORDER BY time DESC LIMIT %5 FORMAT TabSeparated")
        .arg(QString::fromLatin1(kLiveColumns), signColumn, call)
        .arg(qMax(1, minutes))
        .arg(qMax(1, limit));
}

std::optional<LiveSpot> parseLiveRow(const QString &line)
{
    const QStringList c = line.split('\t');
    if (c.size() < 10)
        return std::nullopt;
    LiveSpot s;
    bool ok = false;
    s.band = c[0].toInt(&ok);
    if (!ok)
        return std::nullopt;
    s.rxSign = c[1].trimmed();
    s.rxLat = c[2].toDouble();
    s.rxLon = c[3].toDouble();
    s.rxLoc = c[4].trimmed();
    s.txSign = c[5].trimmed();
    s.txLat = c[6].toDouble();
    s.txLon = c[7].toDouble();
    s.txLoc = c[8].trimmed();
    s.distance = c[9].trimmed().toInt();
    return s;
}

QString liveBaseUrl()
{
    const QString env = qEnvironmentVariable("WSPRMAP_LIVE_URL");
    return env.isEmpty() ? QString("http://db1.wspr.live/") : env;
}

namespace {
// blocking GET in this thread (C#: GetWsprLiveResults with up to 3 retries); empty optional = failed
std::optional<QString> liveGet(const QString &sql, QString *err)
{
    QNetworkAccessManager net;
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (attempt > 0)
            QThread::msleep(800);
        QUrl url(liveBaseUrl());
        url.setQuery("query=" + QString::fromLatin1(QUrl::toPercentEncoding(sql)), QUrl::StrictMode);
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, "WSPR Scheduler Map 3");
        req.setTransferTimeout(30000);
        QNetworkReply *reply = net.get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QString body = QString::fromUtf8(reply->readAll());
        if (!ok && err)
            *err = reply->errorString() + (body.isEmpty() ? QString() : ": " + body.left(150).trimmed());
        reply->deleteLater();
        if (ok)
            return body;
    }
    return std::nullopt;
}

// C#: GetWsprLiveResults for "tx" (tx_sign = call: who heard it) or "rx" (rx_sign = call: what it heard)
bool liveSpots(const Request &r, bool txSide, int minutes, Result &res, bool *homeSet)
{
    const QString call = r.liveCall.trimmed().toUpper();
    QString sql = liveQuery(txSide ? "tx_sign" : "rx_sign", call, minutes, r.liveLimit);
    // band and distance in the query too, so the limit isn't used up by spots that would be skipped
    QString extra;
    const int band = bandNumber(r.bandIndex);
    if (band != -2)
        extra += QString(" AND band = %1").arg(band);
    if (r.minKm > 0)
        extra += QString(" AND distance >= %1").arg(int(std::ceil(r.minKm)));
    if (!extra.isEmpty())
        sql.replace(" ORDER BY", extra + " ORDER BY");
    QString err;
    const auto body = liveGet(sql, &err);
    if (!body) {
        res.error = "Unable to get reports from wspr.live: " + err;
        return false;
    }
    for (const QString &line : body->split('\n', Qt::SkipEmptyParts)) {
        const auto s = parseLiveRow(line);
        if (!s)
            continue;
        if (!*homeSet) {   // C#: the first spot gives this station's position and the header
            res.data.hasHome = true;
            res.data.home = txSide ? LatLon{s->txLat, s->txLon} : LatLon{s->rxLat, s->rxLon};
            res.data.homeCall = txSide ? s->txSign : s->rxSign;
            res.data.homeLocator = txSide ? s->txLoc : s->rxLoc;
            *homeSet = true;
        }
        const QString other = txSide ? s->rxSign : s->txSign;
        if (s->distance < r.minKm || (band != -2 && s->band != band) || (r.hideSpecial && specialCall(other)))
            continue;
        if (const auto p = locatorToLatLon(txSide ? s->rxLoc : s->txLoc))
            res.data.add(!txSide, p->lat, p->lon, other + " " + bandLabel(s->band));
    }
    return true;
}
} // namespace

// ---- one update ----------------------------------------------------------------------------------

Result buildMap(const Request &r, const DbSettings &db)
{
    Result res;
    res.data.showPaths = r.showPaths;
    const bool wantTx = r.show != Show::RxOnly, wantRx = r.show != Show::TxOnly;

    if (r.live) {
        const QString call = r.liveCall.trimmed().toUpper();
        if (!validCall(call)) {
            res.error = call.isEmpty() ? "Callsign empty" : "Callsign \"" + call + "\" not valid";
            return res;
        }
        int minutes = int(std::ceil(periodHours(r.periodIndex) * 60));
        if (minutes > 24 * 60) {
            res.notice = "WSPR Live max period 24 hrs - defaulting to 24 hrs";
            minutes = 24 * 60;
        }
        bool homeSet = false;
        if (wantTx)
            liveSpots(r, true, minutes, res, &homeSet);
        if (wantRx)
            liveSpots(r, false, minutes, res, &homeSet);
        if (!homeSet && res.error.isEmpty())
            res.notice = "No reports on wspr.live for " + call + " in this period";
        return res;
    }

    // local databases
    if (!mysqlAnswers(db)) {
        res.databaseDown = true;
        res.error = QString("Can't connect to MySQL server on '%1' (port %2 not answering)").arg(db.host).arg(db.port);
        return res;
    }
    QString homeLoc = r.homeLocator, homeCall = r.homeCall;
    if (homeLoc.isEmpty() || homeCall.isEmpty()) {   // C#: not found at start-up: try again
        QString err;
        if (const auto st = findOwnStation(db, &err)) {
            homeLoc = st->locator;
            homeCall = st->call;
        } else if (!err.isEmpty() && !err.startsWith("no reports")) {
            res.databaseDown = true;
            res.error = err;
            return res;
        }
    }
    const auto home = locatorToLatLon(homeLoc);
    if (!home) {
        res.error = "Cannot load map - locator not found in database.";
        return res;
    }
    res.data.hasHome = true;
    res.data.home = *home;
    res.data.homeCall = homeCall;
    res.data.homeLocator = homeLoc;

    const QDateTime now = (r.nowUtc.isValid() ? r.nowUtc : QDateTime::currentDateTimeUtc()).toUTC();
    const QDateTime from = periodStart(now, r.periodIndex);
    QString err;
    if (wantRx && !readReceived(r, db, from, now, res.data, &err))
        res.error = "Error reading wspr_rpt.received: " + err;
    err.clear();
    if (wantTx && !readReported(r, db, from, now, res.data, &err) && !err.contains("doesn't exist"))
        res.error = (res.error.isEmpty() ? QString() : res.error + "\n") + "Error reading wspr_rx.reported: " + err;
    return res;
}

} // namespace Map
