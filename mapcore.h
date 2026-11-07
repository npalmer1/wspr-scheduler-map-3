#pragma once
// WSPR Scheduler Map 3, without the window (C#: Wspr_Map Form1 - everything except the WebView2 map):
//  - Maidenhead locators, map projection, great-circle paths, the day/night line;
//  - the band / period / de-clutter lists of the right-hand panel;
//  - reading the reports from the local databases (wspr_rpt.received: stations this station heard,
//    written by WSPR Scheduler; wspr_rx.reported: stations that heard this station, written by
//    WSPR Scheduler Live) or straight from wspr.live.
// Everything here can run in a worker thread: database reads open a connection of their own.
#include <QDateTime>
#include <QPointF>
#include <QSet>
#include <QStringList>
#include <QString>
#include <QVector>
#include <optional>

namespace Map {

struct LatLon
{
    double lat = 0, lon = 0;
};

// C#: MaidenheadLocator.LocatorToLatLng - the centre of a 4, 6, 8 or 10 character square.
// nullopt for anything else (C# returned 0,0, which put a pin off the coast of Africa).
std::optional<LatLon> locatorToLatLon(const QString &locator);

// ---- Web Mercator (the OpenStreetMap tiles) ---------------------------------------------------
constexpr int kTileSize = 256;
constexpr int kMinZoom = 2, kMaxZoom = 16;   // as wspr_map.html
constexpr double kMaxLat = 85.0511287798;    // the top and bottom of the square world map
double worldSize(int zoom);                  // width (= height) of the whole world in pixels
QPointF project(double lat, double lon, int zoom);   // pixels from the top left of the world (lon -180)
LatLon unproject(const QPointF &px, int zoom);

// ---- paths and night ---------------------------------------------------------------------------
// great-circle path as (lon, lat) points, longitudes "unwrapped" so the line is continuous
// (they may go past +-180); empty if both ends are the same place. C#/JS: addGreatCirclePath
QVector<QPointF> greatCircle(double lat1, double lon1, double lat2, double lon2, int steps = 64);

// the day/night line for the given UTC time as (lon, lat) points from lon -180 to 180, and which
// pole is in the dark. C#/JS: drawNight
struct Terminator
{
    QVector<QPointF> line;
    bool southDark = true;   // sun north of the equator: the south pole is in darkness
};
Terminator terminator(const QDateTime &utc);

// ---- the right-hand lists ----------------------------------------------------------------------
QStringList periodNames();               // "10 mins" ... "28 days"
double periodHours(int index);           // C#: findPeriod (0 if none selected)
QDateTime periodStart(const QDateTime &nowUtc, int index);   // C#: the dtPrev calculation
QStringList bandNames();                 // "All", "LF", "MF", "160m" ...
QString bandFrequencyPrefix(int index);  // C#: find_band - for "frequency LIKE '14%'" ("" = all)
int bandNumber(int index);               // C#: get_band - wspr.live / reported band (-2 = all)
QString bandLabel(int band);             // C#: get_reverse_band - 14 -> "20"
QStringList declutterSteps(bool km);     // C#: kmcheckBox_CheckedChanged lists
double milesToKm(double miles);
bool specialCall(const QString &call);   // C#: specialCall - Q..., 0..., 1..., two digits

// ---- what the map shows ------------------------------------------------------------------------
struct Point
{
    double lat = 0, lon = 0;
    QString label;
};

struct MapData
{
    bool hasHome = false;
    LatLon home;
    QString homeCall, homeLocator;
    QVector<Point> rx;      // stations this station heard (blue pins, green paths)
    QVector<Point> tx;      // stations that heard this station (red pins, purple paths)
    bool showPaths = true;

    // C#: AccumulateMarker - one pin per position (same square = same dot)
    void add(bool rxPoint, double lat, double lon, const QString &label);
    void clearPoints();

private:
    QSet<QString> m_rxSeen, m_txSeen;
};

enum class Show { Both, TxOnly, RxOnly };   // C#: radioButton1 / radioButton2 / radioButton3

// one update of the map (C#: filter_results), with everything it needs from the window
struct Request
{
    bool live = false;           // from wspr.live instead of the local databases
    QString liveCall;            // live: the call to ask about
    int periodIndex = 5;         // 3 hours
    int bandIndex = 0;           // all
    double minKm = 0;            // de-clutter: only stations at least this far away
    bool hideSpecial = false;    // QcheckBox
    Show show = Show::Both;
    bool showPaths = true;
    QString homeLocator;         // local: this station (found from the database if empty)
    QString homeCall;
    int dbRows = 300000;         // C#: dbRows
    int liveLimit = 5000;        // C#: liveLimit
    QDateTime nowUtc;            // empty = now
};

struct Result
{
    MapData data;
    QString error;               // why there is nothing (or less) to show; empty = fine
    bool databaseDown = false;   // local: couldn't open the databases at all
    QString notice;              // something to tell the user that isn't an error
    QString dbCall, dbLocator;   // local: this station as the database has it (even when Config's are used)
};

// ---- local databases -----------------------------------------------------------------------------
struct DbSettings
{
    QString host = "127.0.0.1";
    QString user = "admin";
    QString password = "wspr";
    int port = 3306;
    QString rptDatabase = "wspr_rpt";   // received (tests use scratch databases)
    QString rxDatabase = "wspr_rx";     // reported
};

// quick (0.3 s) check that MySQL answers at all, before a connection that could wait seconds
bool mysqlAnswers(const DbSettings &db);

// C#: find_reportedRX - this station's call and locator, from the newest reports it received
struct Station
{
    QString call, locator;
};
std::optional<Station> findOwnStation(const DbSettings &db, QString *err);

// this station as typed in Config (~/WSPRmapstation: the call on the first line, the locator on the
// second) - used instead of the database's when set, e.g. before WSPR Scheduler has received anything
Station readStation(const QString &homeDir = QString());
bool writeStation(const Station &st, const QString &homeDir = QString());

// C#: EnsureIndexes - the indexes that make the time searches quick (errors ignored)
void ensureIndexes(const DbSettings &db);

// ---- wspr.live -------------------------------------------------------------------------------------
QString liveQuery(const QString &signColumn, const QString &call, int minutes, int limit);
bool validCall(const QString &call);
struct LiveSpot
{
    int band = 0;
    QString rxSign, rxLoc, txSign, txLoc;
    double rxLat = 0, rxLon = 0, txLat = 0, txLon = 0;
    int distance = 0;
};
std::optional<LiveSpot> parseLiveRow(const QString &line);
QString liveBaseUrl();                         // http://db1.wspr.live/ (WSPRMAP_LIVE_URL for tests)

// the whole update (C#: filter_results + find_selectedRX/TX or GetWsprLiveResults); blocking, so
// call it from a worker thread
Result buildMap(const Request &req, const DbSettings &db);

} // namespace Map
