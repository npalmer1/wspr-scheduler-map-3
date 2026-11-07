// Map 3 core: locators, projection, great circles, the day/night line, the right-hand lists, wspr.live
// (a fake server) and the local databases (SCRATCH databases wspr_map_test_rpt / wspr_map_test_rx on a
// local MySQL, never the real wspr_rpt / wspr_rx; skipped if there's no MySQL).
#include "mapcore.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
static bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

// ---- a fake wspr.live, in its own thread (buildMap blocks this one) ----------------------------------
class FakeLive : public QObject
{
public:
    QString lastQuery;
    int port = 0;
    void start()
    {
        server = new QTcpServer(this);
        server->listen(QHostAddress::LocalHost);
        port = server->serverPort();
        connect(server, &QTcpServer::newConnection, this, [this] {
            QTcpSocket *s = server->nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                const QByteArray req = s->readAll();
                const QByteArray target = req.split(' ').value(1);
                const QString q = QUrlQuery(QUrl(QString::fromLatin1(target)).query()).queryItemValue(
                    "query", QUrl::FullyDecoded);
                lastQuery = q;
                QByteArray body;
                // band, rx_sign, rx_lat, rx_lon, rx_loc, tx_sign, tx_lat, tx_lon, tx_loc, distance
                if (q.contains("tx_sign LIKE")) {   // who heard G4GCI
                    body = "14\tVK2ABC\t-33.9\t151.2\tQF56\tG4GCI\t51.3\t-1.0\tIO90GU\t17000\n"
                           "14\tW1AW\t41.7\t-72.7\tFN31\tG4GCI\t51.3\t-1.0\tIO90GU\t5300\n"
                           "7\tDL1XX\t50.0\t8.0\tJO40\tG4GCI\t51.3\t-1.0\tIO90GU\t650\n"
                           "14\tQ1TEST\t10.0\t10.0\tJK10\tG4GCI\t51.3\t-1.0\tIO90GU\t4000\n"
                           "garbage line\n";
                } else {                              // what G4GCI heard
                    body = "14\tG4GCI\t51.3\t-1.0\tIO90GU\tJA1XYZ\t35.6\t139.7\tPM95\t9500\n"
                           "14\tG4GCI\t51.3\t-1.0\tIO90GU\tJA1QQQ\t35.6\t139.7\tPM95\t9500\n";   // same square
                }
                const QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: text/tab-separated-values\r\nContent-Length: "
                                        + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                s->write(head + body);
                s->disconnectFromHost();
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        });
    }
private:
    QTcpServer *server = nullptr;
};

static void testPure()
{
    using namespace Map;
    // C# MaidenheadLocator.LocatorToLatLng: IO90GU
    const auto io = locatorToLatLon("io90gu");
    CHECK(io && near(io->lon, ('I' - 'A') * 20 + 9 * 2 + ('G' - 'A' + 0.5) / 12 - 180)
          && near(io->lat, ('O' - 'A') * 10 + 0 + ('U' - 'A' + 0.5) / 24 - 90));
    CHECK(near(io->lat, 50.8541666667, 1e-6) && near(io->lon, -1.4583333333, 1e-6));
    const auto jj = locatorToLatLon("JJ00");
    CHECK(jj && near(jj->lat, 0.5) && near(jj->lon, 1.0));
    CHECK(locatorToLatLon("IO90GU45") && locatorToLatLon("IO90GU45AB"));
    CHECK(!locatorToLatLon("") && !locatorToLatLon("ZZ99") && !locatorToLatLon("IO9") && !locatorToLatLon("IO90G"));

    // projection
    const QPointF c = project(0, 0, 2);
    CHECK(near(c.x(), 512) && near(c.y(), 512));
    const LatLon back = unproject(project(51.5, -0.12, 7), 7);
    CHECK(near(back.lat, 51.5, 1e-9) && near(back.lon, -0.12, 1e-9));
    CHECK(near(project(89.9, 0, 2).y(), 0, 1e-6));   // clamped at the top of the map

    // great circles: ends, continuity, unwrapped across 180
    const auto gc = greatCircle(51.3, -1.0, -33.9, 151.2, 64);
    CHECK(gc.size() == 65 && near(gc.first().y(), 51.3, 1e-9) && near(gc.last().y(), -33.9, 1e-9));
    bool smooth = true;
    for (int i = 1; i < gc.size(); ++i)
        if (std::abs(gc[i].x() - gc[i - 1].x()) > 20)
            smooth = false;
    CHECK(smooth);
    const auto pac = greatCircle(35.6, 139.7, 34.0, -118.2);   // Tokyo - Los Angeles: over the Pacific
    CHECK(!pac.isEmpty() && pac.last().x() > 180 && near(pac.last().x() - 360, -118.2, 1e-6));
    CHECK(greatCircle(10, 10, 10, 10).isEmpty());

    // day/night: midsummer noon UTC - the sun is over the north at lon 0, the south pole is dark,
    // and at lon 0 the line is near the Antarctic circle
    const Terminator t = terminator(QDateTime(QDate(2026, 6, 21), QTime(12, 0), QTimeZone::utc()));
    CHECK(t.southDark && t.line.size() == 721);
    CHECK(near(t.line[360].x(), 0) && t.line[360].y() < -60 && t.line[360].y() > -70);
    const Terminator w = terminator(QDateTime(QDate(2026, 12, 21), QTime(0, 0), QTimeZone::utc()));
    CHECK(!w.southDark);

    // lists (C#: findPeriod / find_band / get_band / get_reverse_band / specialCall)
    CHECK(periodNames().size() == 18 && bandNames().size() == 20);
    const QDateTime now(QDate(2026, 11, 7), QTime(10, 0), QTimeZone::utc());
    CHECK(periodStart(now, 0) == now.addSecs(-600) && periodStart(now, 5) == now.addSecs(-3 * 3600)
          && periodStart(now, 17) == now.addDays(-28));
    CHECK(bandNumber(9) == 14 && bandNumber(0) == -2 && bandLabel(14) == "20" && bandLabel(432) == "70cm");
    CHECK(bandFrequencyPrefix(9) == "14" && bandFrequencyPrefix(0).isEmpty() && bandFrequencyPrefix(5) == "5.");
    CHECK(declutterSteps(true).value(1) == "80" && declutterSteps(false).value(1) == "50");
    CHECK(specialCall("Q1ABC") && specialCall("0XX") && specialCall("1AB") && specialCall("22AB")
          && !specialCall("G4GCI") && !specialCall("2E0ABC"));

    // one pin per position
    MapData d;
    d.add(true, 50.001, 1.0, "A");
    d.add(true, 50.002, 1.0, "B");   // same square to 2 decimals
    d.add(false, 50.001, 1.0, "C");  // the other list
    CHECK(d.rx.size() == 1 && d.rx[0].label == "A" && d.tx.size() == 1);

    // wspr.live
    const QString q = liveQuery("tx_sign", "G4GCI", 180, 5000);
    CHECK(q.contains("tx_sign LIKE '%G4GCI%'") && q.contains("subtractMinutes(now(), 180)") && q.contains("LIMIT 5000")
          && q.endsWith("FORMAT TabSeparated") && !q.contains("SELECT *"));
    CHECK(validCall("G4GCI") && validCall("G4GCI/P") && !validCall("G4' OR 1=1") && !validCall(""));
    const auto row = parseLiveRow("14\tW1AW\t41.7\t-72.7\tFN31\tG4GCI\t51.3\t-1.0\tIO90GU\t5300");
    CHECK(row && row->band == 14 && row->rxSign == "W1AW" && row->txLoc == "IO90GU" && row->distance == 5300);
    CHECK(!parseLiveRow("nonsense"));
}

static void testStationFile()
{
    QTemporaryDir home;
    CHECK(Map::readStation(home.path()).call.isEmpty());
    CHECK(Map::writeStation({"g4gci", "io90gu"}, home.path()));
    const Map::Station st = Map::readStation(home.path());
    CHECK(st.call == "G4GCI" && st.locator == "IO90GU");
    CHECK(Map::writeStation({"", ""}, home.path()) && Map::readStation(home.path()).call.isEmpty());   // cleared
    CHECK(Map::writeStation({"G4GCI", "XX99"}, home.path()) && Map::readStation(home.path()).locator.isEmpty());
}

static void testLive()
{
    QThread th;
    FakeLive fake;
    fake.moveToThread(&th);
    th.start();
    QMetaObject::invokeMethod(&fake, [&] { fake.start(); }, Qt::BlockingQueuedConnection);
    qputenv("WSPRMAP_LIVE_URL", QString("http://127.0.0.1:%1/").arg(fake.port).toLatin1());

    Map::Request r;
    r.live = true;
    r.liveCall = "g4gci";
    r.periodIndex = 5;
    const Map::Result res = Map::buildMap(r, Map::DbSettings());
    CHECK(res.error.isEmpty() && res.data.hasHome && res.data.homeCall == "G4GCI" && res.data.homeLocator == "IO90GU");
    CHECK(near(res.data.home.lat, 51.3) && near(res.data.home.lon, -1.0));
    CHECK(res.data.tx.size() == 4 && res.data.rx.size() == 1);   // JA1QQQ: same square as JA1XYZ
    bool vk = false;
    for (const auto &p : res.data.tx)
        if (p.label == "VK2ABC 20")
            vk = true;
    CHECK(vk);

    r.minKm = 1000;          // DL1XX is 650 km away
    r.hideSpecial = true;    // Q1TEST
    r.show = Map::Show::TxOnly;
    r.periodIndex = 17;      // 28 days: more than wspr.live's 24 hours
    const Map::Result f = Map::buildMap(r, Map::DbSettings());
    CHECK(f.data.tx.size() == 2 && f.data.rx.isEmpty() && f.notice.contains("24 hrs"));
    CHECK(fake.lastQuery.contains("distance >= 1000") && fake.lastQuery.contains("subtractMinutes(now(), 1440)"));

    r.liveCall = "";
    CHECK(Map::buildMap(r, Map::DbSettings()).error == "Callsign empty");

    qputenv("WSPRMAP_LIVE_URL", "http://127.0.0.1:1/");   // nothing there
    r.liveCall = "G4GCI";
    const Map::Result down = Map::buildMap(r, Map::DbSettings());
    CHECK(down.error.startsWith("Unable to get reports from wspr.live"));
    qunsetenv("WSPRMAP_LIVE_URL");
    th.quit();
    th.wait();
}

static bool exec(QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        std::printf("  sql: %s\n  -> %s\n", qPrintable(sql.left(120)), qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

static void testDatabase()
{
    Map::DbSettings s;
    s.password = qEnvironmentVariable("WSPRMAP_TEST_DBPASS", "wspr");
    s.rptDatabase = "wspr_map_test_rpt";
    s.rxDatabase = "wspr_map_test_rx";
    if (!Map::mysqlAnswers(s)) {
        std::printf("map_test: no local MySQL - database part skipped\n");
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "setup");
        db.setHostName(s.host);
        db.setUserName(s.user);
        db.setPassword(s.password);
        CHECK(db.open());
        const QString now = QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss");
        const QString old = QDateTime::currentDateTimeUtc().addDays(-3).toString("yyyy-MM-dd HH:mm:ss");
        CHECK(exec(db, "DROP DATABASE IF EXISTS wspr_map_test_rpt") && exec(db, "DROP DATABASE IF EXISTS wspr_map_test_rx")
              && exec(db, "CREATE DATABASE wspr_map_test_rpt") && exec(db, "CREATE DATABASE wspr_map_test_rx"));
        CHECK(exec(db, "CREATE TABLE wspr_map_test_rpt.received (datetime DATETIME NOT NULL, band SMALLINT NOT NULL, "
                       "tx_sign VARCHAR(15) NOT NULL, tx_loc VARCHAR(10) NOT NULL, frequency DOUBLE NOT NULL, "
                       "power SMALLINT NOT NULL DEFAULT 0, snr MEDIUMINT NOT NULL DEFAULT 0, drift SMALLINT NOT NULL DEFAULT 0, "
                       "distance INT NOT NULL, azimuth SMALLINT NOT NULL DEFAULT 0, reporter VARCHAR(20) NOT NULL, "
                       "reporter_loc VARCHAR(10) NOT NULL, dt FLOAT NOT NULL DEFAULT 0, "
                       "PRIMARY KEY (datetime, tx_sign, frequency))"));
        CHECK(exec(db, "INSERT INTO wspr_map_test_rpt.received (datetime, band, tx_sign, tx_loc, frequency, distance, reporter, reporter_loc) VALUES "
                       "('" + now + "', 14, 'DL1AB', 'JO40', 14.0971, 650, 'G4GCI', 'IO90GU'),"
                       "('" + now + "', 7, 'F5XYZ', 'JN18', 7.0401, 400, 'G4GCI', 'IO90GU'),"
                       "('" + now + "', 14, 'EA5AA', 'IM98', 14.0971, 1500, 'G4GCI', 'IO90GU'),"
                       "('" + now + "', 14, 'nil rcvd', '', 14.0971, 0, 'G4GCI', 'IO90GU'),"
                       "('" + now + "', 14, 'BADLOC', 'ZZ', 14.0971, 0, 'G4GCI', 'IO90GU'),"
                       "('" + old + "', 14, 'OLD1', 'KP20', 14.0971, 1800, 'G4GCI', 'IO90GU')"));
        CHECK(exec(db, "CREATE TABLE wspr_map_test_rx.reported (id BIGINT NOT NULL PRIMARY KEY, time DATETIME NOT NULL, "
                       "band SMALLINT NOT NULL, rx_sign VARCHAR(20) NOT NULL, rx_loc VARCHAR(8) NOT NULL, "
                       "distance INT NOT NULL, frequency INT NOT NULL DEFAULT 0)"));
        CHECK(exec(db, "INSERT INTO wspr_map_test_rx.reported VALUES "
                       "(1, '" + now + "', 14, 'VK2ABC', 'QF56', 17000, 14097100),"
                       "(2, '" + now + "', 7, 'W1AW', 'FN31', 5300, 7040100),"
                       "(3, '" + now + "', 14, 'Q0TEST', 'JK10', 4000, 14097100)"));
        db.close();
    }
    QSqlDatabase::removeDatabase("setup");

    QString err;
    const auto st = Map::findOwnStation(s, &err);
    CHECK(st && st->call == "G4GCI" && st->locator == "IO90GU");

    Map::Request r;   // 3 hours, all bands, both
    Map::Result res = Map::buildMap(r, s);
    CHECK(res.error.isEmpty() && !res.databaseDown && res.data.hasHome && res.data.homeCall == "G4GCI");
    CHECK(res.data.rx.size() == 3 && res.data.tx.size() == 3);   // not OLD1, nil rcvd or BADLOC
    bool label = false;
    for (const auto &p : res.data.rx)
        if (p.label == "DL1AB (20)")
            label = true;
    CHECK(label);

    r.bandIndex = 9;   // 20 m: frequency LIKE '14%' / band = 14
    r.hideSpecial = true;
    r.minKm = 1000;
    res = Map::buildMap(r, s);
    CHECK(res.data.rx.size() == 1 && res.data.rx[0].label == "EA5AA" && res.data.tx.size() == 1
          && res.data.tx[0].label == "VK2ABC");

    r = Map::Request();
    r.periodIndex = 12;   // 4 days: OLD1 too
    r.show = Map::Show::RxOnly;
    res = Map::buildMap(r, s);
    CHECK(res.data.rx.size() == 4 && res.data.tx.isEmpty());

    Map::ensureIndexes(s);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "check");
        db.setHostName(s.host);
        db.setUserName(s.user);
        db.setPassword(s.password);
        db.open();
        QSqlQuery q(db);
        q.exec("SELECT COUNT(DISTINCT index_name) FROM information_schema.statistics WHERE table_schema IN "
               "('wspr_map_test_rpt', 'wspr_map_test_rx') AND index_name LIKE 'idx_%'");
        CHECK(q.next() && q.value(0).toInt() == 4);
        Map::ensureIndexes(s);   // again: nothing to do, no error
        exec(db, "DROP DATABASE wspr_map_test_rpt");
        exec(db, "DROP DATABASE wspr_map_test_rx");
        db.close();
    }
    QSqlDatabase::removeDatabase("check");

    // no reports received yet: Config's call and locator
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "empty");
        db.setHostName(s.host);
        db.setUserName(s.user);
        db.setPassword(s.password);
        db.open();
        exec(db, "CREATE DATABASE wspr_map_test_rpt");
        exec(db, "CREATE TABLE wspr_map_test_rpt.received (datetime DATETIME NOT NULL, band SMALLINT, tx_sign VARCHAR(15), "
                 "tx_loc VARCHAR(10), frequency DOUBLE, distance INT, reporter VARCHAR(20), reporter_loc VARCHAR(10))");
        Map::Request none;
        CHECK(!Map::buildMap(none, s).data.hasHome && Map::buildMap(none, s).error.contains("Config"));
        none.homeCall = "G4GCI";
        none.homeLocator = "IO90GU";
        const Map::Result typed = Map::buildMap(none, s);
        CHECK(typed.error.isEmpty() && typed.data.hasHome && typed.data.homeCall == "G4GCI"
              && std::abs(typed.data.home.lat - 50.854) < 0.01);
        exec(db, "DROP DATABASE wspr_map_test_rpt");
        db.close();
    }
    QSqlDatabase::removeDatabase("empty");

    Map::DbSettings wrong = s;
    wrong.port = 1;   // nothing listening: quick
    res = Map::buildMap(Map::Request(), wrong);
    CHECK(res.databaseDown && res.error.contains("not answering"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    testPure();
    testStationFile();
    testLive();
    testDatabase();
    std::printf(failures ? "map_test: %d FAILED\n" : "map_test: all passed\n", failures);
    return failures ? 1 : 0;
}
