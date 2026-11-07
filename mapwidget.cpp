// MapWidget - see mapwidget.h.
#include "mapwidget.h"

#include <QDir>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPainter>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <cmath>

#ifndef WSPRMAP_VERSION
#define WSPRMAP_VERSION "0"
#endif

namespace {
const QColor kBackground(0x0a, 0x15, 0x20);        // wspr_map.html body
const QColor kTxPin(0xcc, 0x00, 0x00);             // red: stations that heard this station
const QColor kRxPin(0x1a, 0x3a, 0xcf);             // blue: stations this station heard
const QColor kTxPath(0x7b, 0x00, 0xd4, 160);       // purple lines (wspr_map.html #7b00d4)
const QColor kRxPath(0x00, 0x9a, 0x00, 150);       // green lines
const QColor kHome(0x00, 0xc8, 0xff);
constexpr int kMaxDownloads = 2;                   // OpenStreetMap tile rules
constexpr int kButton = 26;
}

QString MapWidget::defaultTileUrl()
{
    const QString env = qEnvironmentVariable("WSPRMAP_TILES");
    return env.isEmpty() ? QString("https://tile.openstreetmap.org/{z}/{x}/{y}.png") : env;
}

QString MapWidget::userAgent()
{
    return QString("WSPR-Scheduler-Map3/%1 (+https://github.com/npalmer1/wspr-scheduler-map-3)").arg(WSPRMAP_VERSION);
}

MapWidget::MapWidget(QWidget *parent)
    : QWidget(parent), m_tileUrl(defaultTileUrl()), m_net(new QNetworkAccessManager(this)), m_tiles(600),
      m_nightTimer(new QTimer(this))
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(200, 150);
    setCursor(Qt::OpenHandCursor);
    // the tooltip in wspr_map.html's style
    setStyleSheet("QToolTip { background-color: rgb(6,13,20); color: #8ecfb0; border: 1px solid #00c8ff;"
                  " font-family: Consolas, 'Courier New', monospace; font-size: 9pt; padding: 3px 6px; }");

    auto *cache = new QNetworkDiskCache(this);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    cache->setCacheDirectory(QDir(dir.isEmpty() ? QDir::tempPath() : dir).filePath("tiles"));
    cache->setMaximumCacheSize(300LL * 1024 * 1024);
    m_net->setCache(cache);
    connect(m_net, &QNetworkAccessManager::finished, this, &MapWidget::tileArrived);

    connect(m_nightTimer, &QTimer::timeout, this, [this] {   // JS: setInterval(drawNight, 60000)
        rebuildNight();
        update();
    });
    m_nightTimer->start(60000);

    m_centre = Map::project(20, 0, m_zoom);   // wspr_map.html: centre [20, 0], zoom 2
    rebuild();
}

void MapWidget::setTileUrl(const QString &url)
{
    m_tileUrl = url;
    m_tiles.clear();
    m_failed.clear();
    m_queue.clear();
    update();
}

// ---- data ----------------------------------------------------------------------------------------

void MapWidget::setData(const Map::MapData &data)
{
    m_data = data;
    for (auto &v : m_pathsLonLat)
        v.clear();
    if (m_data.hasHome && m_data.showPaths) {
        for (const auto &pt : m_data.tx)
            m_pathsLonLat[0] << Map::greatCircle(m_data.home.lat, m_data.home.lon, pt.lat, pt.lon);
        for (const auto &pt : m_data.rx)
            m_pathsLonLat[1] << Map::greatCircle(m_data.home.lat, m_data.home.lon, pt.lat, pt.lon);
    }
    rebuild();
    update();
}

void MapWidget::clearData()
{
    setData(Map::MapData());
}

void MapWidget::rebuild()
{
    m_pins.clear();
    for (const auto &pt : m_data.rx)
        m_pins.append({Map::project(pt.lat, pt.lon, m_zoom), pt.label, true});
    for (const auto &pt : m_data.tx)
        m_pins.append({Map::project(pt.lat, pt.lon, m_zoom), pt.label, false});
    for (int k = 0; k < 2; ++k) {
        QPainterPath path;
        for (const auto &line : m_pathsLonLat[k]) {
            if (line.isEmpty())
                continue;
            path.moveTo(Map::project(line[0].y(), line[0].x(), m_zoom));
            for (int i = 1; i < line.size(); ++i)
                path.lineTo(Map::project(line[i].y(), line[i].x(), m_zoom));
        }
        m_paths[k] = path;
    }
    rebuildNight();
}

void MapWidget::rebuildNight()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const Map::Terminator t = Map::terminator(now);
    m_nightLine.clear();
    for (const QPointF &ll : t.line)
        m_nightLine << Map::project(ll.y(), ll.x(), m_zoom);
    m_night = m_nightLine;
    const double pole = t.southDark ? -90 : 90;   // project() stops at the map's edge
    m_night << Map::project(pole, 180, m_zoom) << Map::project(pole, -180, m_zoom);
    m_nightAt = now;
}

// ---- view ----------------------------------------------------------------------------------------

Map::LatLon MapWidget::centre() const
{
    Map::LatLon c = Map::unproject(m_centre, m_zoom);
    while (c.lon > 180) c.lon -= 360;
    while (c.lon < -180) c.lon += 360;
    return c;
}

void MapWidget::centreOn(double lat, double lon, int zoom)
{
    if (zoom >= Map::kMinZoom && zoom <= Map::kMaxZoom && zoom != m_zoom) {
        m_zoom = zoom;
        rebuild();
        emit zoomChanged(m_zoom);
    }
    m_centre = Map::project(lat, lon, m_zoom);
    clampCentre();
    update();
}

void MapWidget::setZoom(int zoom)
{
    zoomAround(zoom, QPointF(width() / 2.0, height() / 2.0));
}

void MapWidget::zoomAround(int newZoom, const QPointF &screenPos)
{
    newZoom = qBound(Map::kMinZoom, newZoom, Map::kMaxZoom);
    if (newZoom == m_zoom)
        return;
    const QPointF fromCentre = screenPos - QPointF(width() / 2.0, height() / 2.0);
    const QPointF under = m_centre + fromCentre;          // this point stays under the mouse
    const double f = std::ldexp(1.0, newZoom - m_zoom);
    m_zoom = newZoom;
    m_centre = under * f - fromCentre;
    clampCentre();
    rebuild();
    emit zoomChanged(m_zoom);
    update();
}

void MapWidget::clampCentre()
{
    const double w = Map::worldSize(m_zoom);
    m_centre.setX(std::fmod(std::fmod(m_centre.x(), w) + w, w));   // the world repeats: keep x in one copy
    const double h = height();
    if (w <= h)
        m_centre.setY(w / 2);
    else
        m_centre.setY(qBound(h / 2, m_centre.y(), w - h / 2));    // JS: maxBounds lat -90..90
}

QRectF MapWidget::viewRect() const
{
    return QRectF(m_centre.x() - width() / 2.0, m_centre.y() - height() / 2.0, width(), height());
}

QList<int> MapWidget::copyOffsets(const QRectF &view) const
{
    // paths may run past +-180 (they are unwrapped), so one extra copy each side
    const double w = Map::worldSize(m_zoom);
    QList<int> k;
    for (int i = int(std::floor(view.left() / w)) - 1; i <= int(std::floor(view.right() / w)) + 1; ++i)
        k << i;
    return k;
}

void MapWidget::resizeEvent(QResizeEvent *)
{
    clampCentre();
}

// ---- drawing -------------------------------------------------------------------------------------

void MapWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBackground);
    const QRectF view = viewRect();
    drawTiles(p, view);

    p.setRenderHint(QPainter::Antialiasing, true);
    const double w = Map::worldSize(m_zoom);
    const QList<int> copies = copyOffsets(view);

    // night side and the day/night line (JS: drawNight)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x00, 0x08, 0x20, 89));
    for (int k : copies) {
        p.save();
        p.translate(k * w - view.left(), -view.top());
        p.drawPolygon(m_night);
        p.restore();
    }
    QPen term(QColor(0xff, 0xdd, 0x66, 153), 1.3);
    term.setCosmetic(true);
    p.setPen(term);
    p.setBrush(Qt::NoBrush);
    for (int k : copies) {
        p.save();
        p.translate(k * w - view.left(), -view.top());
        p.drawPolyline(m_nightLine);
        p.restore();
    }

    // great-circle paths: green from the stations heard, purple to the stations that heard us
    for (int c = 0; c < 2; ++c) {
        QPen pen(c == 0 ? kTxPath : kRxPath, 1.5);
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        for (int k : copies) {
            p.save();
            p.translate(k * w - view.left(), -view.top());
            p.drawPath(m_paths[c]);
            p.restore();
        }
    }

    // pins: blue first, red on top; then home
    const QRectF visible = rect().adjusted(-20, -20, 20, 20);
    for (int pass = 0; pass < 2; ++pass) {
        const bool rxPass = pass == 0;
        for (const Pin &pin : m_pins) {
            if (pin.rx != rxPass)
                continue;
            for (int k : copies) {
                const QPointF at = pin.world + QPointF(k * w - view.left(), -view.top());
                if (visible.contains(at))
                    drawPin(p, at, pin.rx ? kRxPin : kTxPin);
            }
        }
    }
    if (m_data.hasHome) {
        const QPointF home = Map::project(m_data.home.lat, m_data.home.lon, m_zoom);
        for (int k : copies) {
            const QPointF at = home + QPointF(k * w - view.left(), -view.top());
            if (visible.contains(at))
                drawHome(p, at);
        }
    }
    drawControls(p);
}

void MapWidget::drawTiles(QPainter &p, const QRectF &view)
{
    const int n = 1 << m_zoom;
    const int x0 = int(std::floor(view.left() / Map::kTileSize)), x1 = int(std::floor(view.right() / Map::kTileSize));
    const int y0 = qMax(0, int(std::floor(view.top() / Map::kTileSize)));
    const int y1 = qMin(n - 1, int(std::floor(view.bottom() / Map::kTileSize)));
    // ask for the middle ones first
    QStringList wanted;
    QVector<std::pair<double, QString>> order;
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int ty = y0; ty <= y1; ++ty)
        for (int tx = x0; tx <= x1; ++tx) {
            const int wx = ((tx % n) + n) % n;
            const QRectF target(tx * Map::kTileSize - view.left(), ty * Map::kTileSize - view.top(), Map::kTileSize,
                                Map::kTileSize);
            if (QPixmap *pm = m_tiles.object(key(m_zoom, wx, ty))) {
                p.drawPixmap(target, *pm, QRectF(pm->rect()));
                continue;
            }
            // not here yet: a zoomed-up piece of a lower-zoom tile we already have, meanwhile
            for (int up = 1; up <= m_zoom; ++up) {
                const int pz = m_zoom - up, px = wx >> up, py = ty >> up, part = 1 << up;
                if (QPixmap *pm = m_tiles.object(key(pz, px, py))) {
                    const double s = double(pm->width()) / part;
                    p.drawPixmap(target, *pm, QRectF((wx - (px << up)) * s, (ty - (py << up)) * s, s, s));
                    break;
                }
            }
            const QPointF mid = target.center() - QPointF(width() / 2.0, height() / 2.0);
            order.append({mid.x() * mid.x() + mid.y() * mid.y(), key(m_zoom, wx, ty)});
        }
    std::sort(order.begin(), order.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &o : order)
        wanted << o.second;
    // only what's on screen now: drop requests for places we've moved away from
    m_queue.clear();
    for (const QString &k : wanted) {
        if (m_inFlight.contains(k))
            continue;
        const auto f = m_failed.constFind(k);
        if (f != m_failed.constEnd() && f->secsTo(QDateTime::currentDateTimeUtc()) < 30)
            continue;
        m_queue << k;
    }
    QTimer::singleShot(0, this, &MapWidget::pump);
}

void MapWidget::drawPin(QPainter &p, const QPointF &at, const QColor &fill) const
{
    // wspr_map.html makePinIcon: 8 x 14, the point at the bottom; drawn as a path so it is sharp at
    // any screen scaling
    static const QPainterPath shape = [] {
        QPainterPath s;
        s.moveTo(0, 0);
        s.cubicTo(0, 0, -4, -7, -4, -10);
        s.cubicTo(-4, -12.2, -2.2, -14, 0, -14);
        s.cubicTo(2.2, -14, 4, -12.2, 4, -10);
        s.cubicTo(4, -7, 0, 0, 0, 0);
        s.closeSubpath();
        return s;
    }();
    p.save();
    p.translate(at);
    QPen outline(QColor(0x22, 0x22, 0x22), 1.0);
    outline.setJoinStyle(Qt::RoundJoin);
    p.setPen(outline);
    p.setBrush(fill);
    p.drawPath(shape);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 160));
    p.drawEllipse(QPointF(0, -10), 1.5, 1.5);
    p.restore();
}

void MapWidget::drawHome(QPainter &p, const QPointF &at) const
{
    // wspr_map.html homeMarker: 20 x 20 cross with a ringed dot
    p.save();
    p.translate(at);
    QPen line(kHome, 1.5);
    p.setPen(line);
    p.drawLine(QPointF(-10, 0), QPointF(10, 0));
    p.drawLine(QPointF(0, -10), QPointF(0, 10));
    p.setPen(QPen(Qt::white, 1.5));
    p.setBrush(kHome);
    p.drawEllipse(QPointF(0, 0), 5, 5);
    p.restore();
}

QRect MapWidget::zoomInRect() const
{
    return QRect(10, 10, kButton, kButton);
}

QRect MapWidget::zoomOutRect() const
{
    return QRect(10, 10 + kButton, kButton, kButton);
}

void MapWidget::drawControls(QPainter &p)
{
    // Leaflet's zoom control, top left
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRect box = zoomInRect().united(zoomOutRect());
    p.setPen(QColor(0, 0, 0, 90));
    p.setBrush(Qt::white);
    p.drawRect(box.adjusted(0, 0, -1, -1));
    p.setPen(QColor(0xcc, 0xcc, 0xcc));
    p.drawLine(box.left() + 1, zoomOutRect().top(), box.right() - 1, zoomOutRect().top());
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen sign(m_zoom < Map::kMaxZoom ? QColor(0x33, 0x33, 0x33) : QColor(0xbb, 0xbb, 0xbb), 2);
    p.setPen(sign);
    const QPointF a = QRectF(zoomInRect()).center();
    p.drawLine(a + QPointF(-6, 0), a + QPointF(6, 0));
    p.drawLine(a + QPointF(0, -6), a + QPointF(0, 6));
    sign.setColor(m_zoom > Map::kMinZoom ? QColor(0x33, 0x33, 0x33) : QColor(0xbb, 0xbb, 0xbb));
    p.setPen(sign);
    const QPointF b = QRectF(zoomOutRect()).center();
    p.drawLine(b + QPointF(-6, 0), b + QPointF(6, 0));

    // attribution (required by OpenStreetMap)
    const QString credit = QString::fromUtf8("© OpenStreetMap contributors");
    QFont f = font();
    f.setPointSizeF(8);
    p.setFont(f);
    const QRect tr = p.fontMetrics().boundingRect(credit).adjusted(-5, -2, 5, 2);
    const QRect cr(width() - tr.width(), height() - tr.height(), tr.width(), tr.height());
    p.fillRect(cr, QColor(255, 255, 255, 200));
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawText(cr, Qt::AlignCenter, credit);
}

// ---- tiles ---------------------------------------------------------------------------------------

void MapWidget::pump()
{
    while (m_inFlight.size() < kMaxDownloads && !m_queue.isEmpty()) {
        const QString k = m_queue.takeFirst();
        const QStringList zxy = k.split('/');
        requestTile(zxy[0].toInt(), zxy[1].toInt(), zxy[2].toInt());
    }
}

void MapWidget::requestTile(int z, int x, int y)
{
    QString u = m_tileUrl;
    u.replace("{z}", QString::number(z)).replace("{x}", QString::number(x)).replace("{y}", QString::number(y));
    QNetworkRequest req{QUrl(u)};
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    req.setAttribute(QNetworkRequest::User, key(z, x, y));
    req.setTransferTimeout(20000);
    m_inFlight.insert(key(z, x, y));
    m_net->get(req);
}

void MapWidget::tileArrived(QNetworkReply *r)
{
    r->deleteLater();
    const QString k = r->request().attribute(QNetworkRequest::User).toString();
    m_inFlight.remove(k);
    QPixmap pm;
    if (r->error() == QNetworkReply::NoError && pm.loadFromData(r->readAll())) {
        m_tiles.insert(k, new QPixmap(pm));
        m_failed.remove(k);
        ++m_loadedCount;
        update();
        emit tilesChanged();
    } else {
        m_failed.insert(k, QDateTime::currentDateTimeUtc());
    }
    pump();
}

// ---- mouse ---------------------------------------------------------------------------------------

void MapWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (zoomInRect().contains(e->pos())) {
        setZoom(m_zoom + 1);
        return;
    }
    if (zoomOutRect().contains(e->pos())) {
        setZoom(m_zoom - 1);
        return;
    }
    m_dragging = true;
    m_dragFrom = e->pos();
    m_dragCentre = m_centre;
    setCursor(Qt::ClosedHandCursor);
}

void MapWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        m_centre = m_dragCentre - QPointF(e->pos() - m_dragFrom);
        clampCentre();
        update();
        return;
    }
    const bool overButton = zoomInRect().contains(e->pos()) || zoomOutRect().contains(e->pos());
    const QString label = overButton ? QString() : labelAt(e->pos());
    setCursor(overButton || !label.isEmpty() ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    if (label.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(e->globalPosition().toPoint() + QPoint(12, 8), label, this);   // Leaflet "sticky"
}

void MapWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void MapWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (zoomInRect().contains(e->pos()) || zoomOutRect().contains(e->pos()))
        return;
    zoomAround(m_zoom + (e->modifiers() & Qt::ShiftModifier ? -1 : 1), e->position());
}

void MapWidget::wheelEvent(QWheelEvent *e)
{
    const int d = e->angleDelta().y();
    if (d != 0)
        zoomAround(m_zoom + (d > 0 ? 1 : -1), e->position());
    e->accept();
}

void MapWidget::leaveEvent(QEvent *)
{
    QToolTip::hideText();
}

bool MapWidget::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip)   // tooltips are shown from mouseMoveEvent
        return true;
    return QWidget::event(e);
}

QString MapWidget::labelAt(const QPoint &pos) const
{
    const QRectF view = viewRect();
    const double w = Map::worldSize(m_zoom);
    const QList<int> copies = copyOffsets(view);
    if (m_data.hasHome) {
        const QPointF home = Map::project(m_data.home.lat, m_data.home.lon, m_zoom);
        for (int k : copies) {
            const QPointF at = home + QPointF(k * w - view.left(), -view.top());
            if (QLineF(at, pos).length() <= 7)
                return "<b style=\"color:#00c8ff\">" + m_data.homeCall.toHtmlEscaped() + "</b><br>Home Station";
        }
    }
    // the pin drawn last (on top) wins; a pin is the 8 x 14 box above its point
    QStringList hits;
    for (int i = m_pins.size() - 1; i >= 0; --i)
        for (int k : copies) {
            const QPointF at = m_pins[i].world + QPointF(k * w - view.left(), -view.top());
            if (QRectF(at.x() - 5, at.y() - 15, 10, 16).contains(pos)) {
                hits << m_pins[i].label.toHtmlEscaped();
                break;
            }
        }
    if (hits.isEmpty())
        return QString();
    if (hits.size() > 4)   // several pins on top of each other
        return hits.mid(0, 4).join("<br>") + QString("<br>... and %1 more").arg(hits.size() - 4);
    return hits.join("<br>");
}
