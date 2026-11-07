#pragma once
// The map (C#: WebView2 + Leaflet, wspr_map.html), drawn by Qt: OpenStreetMap tiles, the night side
// and the day/night line, great-circle paths, pins and the home station.
//  - drag to move, wheel / double click / the + - buttons to zoom (2 to 16, as Leaflet);
//  - the world repeats left and right, as Leaflet's worldCopyJump;
//  - hover over a pin for its callsign;
//  - tiles are kept on disk, so a restart doesn't download them again (and OpenStreetMap's tile
//    rules are kept: our own User-Agent, at most 2 downloads at a time, the attribution shown).
#include "mapcore.h"

#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QPainterPath>
#include <QPixmap>
#include <QSet>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class MapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapWidget(QWidget *parent = nullptr);

    void setData(const Map::MapData &data);   // C#/JS: wsprMap.loadData
    const Map::MapData &data() const { return m_data; }
    void clearData();                          // C#/JS: wsprMap.clearAll
    void centreOn(double lat, double lon, int zoom = -1);   // C#/JS: wsprMap.centreOn (no animation)
    void setZoom(int zoom);
    int zoom() const { return m_zoom; }
    Map::LatLon centre() const;

    // {z} {x} {y}; default OpenStreetMap, or WSPRMAP_TILES (tests)
    void setTileUrl(const QString &url);
    static QString defaultTileUrl();
    static QString userAgent();
    int tilesLoaded() const { return m_loadedCount; }   // tests
    int tilesPending() const { return m_inFlight.size() + m_queue.size(); }

    // what the tooltip would say at this point of the widget ("" = nothing there)
    QString labelAt(const QPoint &pos) const;

signals:
    void zoomChanged(int zoom);
    void tilesChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;
    bool event(QEvent *e) override;

private:
    struct Pin
    {
        QPointF world;   // at the current zoom
        QString label;
        bool rx;
    };
    QRectF viewRect() const;            // the part of the world (pixels at this zoom) on screen
    void zoomAround(int newZoom, const QPointF &screenPos);
    void clampCentre();
    void rebuild();                     // world positions and paths for this zoom
    void rebuildNight();
    QList<int> copyOffsets(const QRectF &view) const;   // world copies (multiples of the world width) in view
    void drawTiles(QPainter &p, const QRectF &view);
    void drawPin(QPainter &p, const QPointF &at, const QColor &fill) const;
    void drawHome(QPainter &p, const QPointF &at) const;
    void drawControls(QPainter &p);
    QRect zoomInRect() const;
    QRect zoomOutRect() const;
    void requestTile(int z, int x, int y);
    void pump();
    void tileArrived(QNetworkReply *r);
    static QString key(int z, int x, int y) { return QString("%1/%2/%3").arg(z).arg(x).arg(y); }

    Map::MapData m_data;
    QVector<QVector<QPointF>> m_pathsLonLat[2];   // [0] tx, [1] rx: great circles in (lon, lat)
    int m_zoom = 2;
    QPointF m_centre;                   // world pixels at m_zoom
    QVector<Pin> m_pins;
    QPainterPath m_paths[2];
    QPolygonF m_night;
    QPolygonF m_nightLine;
    QDateTime m_nightAt;
    bool m_dragging = false;
    QPoint m_dragFrom;
    QPointF m_dragCentre;

    QString m_tileUrl;
    QNetworkAccessManager *m_net = nullptr;
    QCache<QString, QPixmap> m_tiles;
    QHash<QString, QDateTime> m_failed;  // don't ask again for a while
    QSet<QString> m_inFlight;
    QStringList m_queue;                 // newest view first
    int m_loadedCount = 0;
    QTimer *m_nightTimer = nullptr;
};
