// Map 3's window, offscreen: start-up from SCRATCH databases (wspr_map_test_rpt / wspr_map_test_rx,
// never the real ones), the header and count, pins and tooltips, the paths switch, zoom and the
// Centre buttons, km / miles, wspr.live mode (a fake server) and the database being down.
// Tiles: WSPRMAP_TILES (e.g. file:///.../tiles/{z}/{x}/{y}.png); none = plain background.
// WSPRMAP_SHOT=<file.png> saves a screenshot of the window after start-up.
#include "loaderror.h"
#include "mainwindow.h"
#include "mapwidget.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QElapsedTimer>
#include <QListWidget>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static bool waitFor(const std::function<bool()> &cond, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

static bool exec(QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    if (!q.exec(sql)) {
        std::printf("  sql: %s -> %s\n", qPrintable(sql.left(100)), qPrintable(q.lastError().text()));
        return false;
    }
    return true;
}

// stations around the world (locator, call)
static const char *kHeardBy[][2] = {   // reported: they heard G4GCI
    {"QF56", "VK2ABC"}, {"FN31", "W1AW"}, {"EM12", "K5XYZ"}, {"DM79", "W0ABC"}, {"CN87", "K7ABC"},
    {"JO62", "DL7AA"}, {"KO85", "UA3AA"}, {"KP20", "OH2BH"}, {"JP50", "LA9XX"}, {"IM58", "CT1AA"},
    {"JN45", "I2XYZ"}, {"KM72", "4X1AA"}, {"PM95", "JA1XYZ"}, {"OL72", "BV2AA"}, {"GF05", "LU1AA"},
    {"GG66", "PY1AA"}, {"KG33", "ZS6AA"}, {"RE78", "ZL2AA"}, {"FK52", "VP2AA"}, {"BL11", "KH6AA"},
    {"FN03", "VE3AA"}, {"EN61", "W9AA"}, {"IO91", "G0ABC"}, {"IO83", "M0XYZ"}, {"JO22", "PA3AA"},
    {"Q0", "Q0TEST"}};
static const char *kHeard[][2] = {     // received: G4GCI heard them
    {"JO40", "DL1AB"}, {"JN18", "F5XYZ"}, {"IM98", "EA5AA"}, {"JO65", "OZ1AA"}, {"KN04", "LZ1AA"},
    {"FN42", "W1XX"}, {"EL98", "W4AA"}, {"OJ11", "9M2AA"}, {"MK82", "VU2AA"}, {"LL57", "A41AA"},
    {"IQ50", "TF3AA"}, {"HM77", "D4AA"}, {"JN58", "DL9MS"}, {"IO64", "GM4AA"}};

class FakeLive : public QObject
{
public:
    int port = 0;
    void start()
    {
        auto *server = new QTcpServer(this);
        server->listen(QHostAddress::LocalHost);
        port = server->serverPort();
        connect(server, &QTcpServer::newConnection, this, [server] {
            QTcpSocket *s = server->nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [s] {
                const QByteArray req = QByteArray::fromPercentEncoding(s->readAll());
                QByteArray body;
                if (req.contains("tx_sign LIKE"))
                    body = "14\tVK3XYZ\t-37.8\t145.0\tQF22\tW1AW\t41.7\t-72.7\tFN31\t16700\n"
                           "14\tG4GCI\t50.8\t-1.4\tIO90GU\tW1AW\t41.7\t-72.7\tFN31\t5300\n";
                const QByteArray head = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size())
                                        + "\r\nConnection: close\r\n\r\n";
                s->write(head + body);
                s->disconnectFromHost();
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        });
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    QTemporaryDir home;   // no ~/DBmapcredential of the real user

    Map::DbSettings s;
    s.password = qEnvironmentVariable("WSPRMAP_TEST_DBPASS", "wspr");
    s.rptDatabase = "wspr_map_test_rpt";
    s.rxDatabase = "wspr_map_test_rx";
    if (!Map::mysqlAnswers(s)) {
        std::printf("mapwindow_uitest: no scratch database; skipped\n");
        return 77;
    }
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "setup");
        db.setHostName(s.host);
        db.setUserName(s.user);
        db.setPassword(s.password);
        CHECK(db.open());
        const QString now = QDateTime::currentDateTimeUtc().addSecs(-120).toString("yyyy-MM-dd HH:mm:00");
        exec(db, "DROP DATABASE IF EXISTS wspr_map_test_rpt");
        exec(db, "DROP DATABASE IF EXISTS wspr_map_test_rx");
        exec(db, "CREATE DATABASE wspr_map_test_rpt");
        exec(db, "CREATE DATABASE wspr_map_test_rx");
        CHECK(exec(db, "CREATE TABLE wspr_map_test_rpt.received (datetime DATETIME NOT NULL, band SMALLINT NOT NULL, "
                       "tx_sign VARCHAR(15) NOT NULL, tx_loc VARCHAR(10) NOT NULL, frequency DOUBLE NOT NULL, "
                       "distance INT NOT NULL, reporter VARCHAR(20) NOT NULL, reporter_loc VARCHAR(10) NOT NULL)"));
        CHECK(exec(db, "CREATE TABLE wspr_map_test_rx.reported (id BIGINT NOT NULL PRIMARY KEY, time DATETIME NOT NULL, "
                       "band SMALLINT NOT NULL, rx_sign VARCHAR(20) NOT NULL, rx_loc VARCHAR(8) NOT NULL, distance INT NOT NULL)"));
        int id = 1;
        for (const auto &h : kHeard)
            exec(db, QString("INSERT INTO wspr_map_test_rpt.received VALUES ('%1', 14, '%2', '%3', 14.0971, 1000, 'G4GCI', 'IO90GU')")
                         .arg(now, h[1], h[0]));
        for (const auto &h : kHeardBy)
            exec(db, QString("INSERT INTO wspr_map_test_rx.reported VALUES (%1, '%2', %3, '%4', '%5', 3000)")
                         .arg(id++).arg(now).arg(id % 3 ? 14 : 7).arg(h[1], h[0]));
        db.close();
    }
    QSqlDatabase::removeDatabase("setup");

    MainWindow w;
    w.setHomeDir(home.path());
    w.setDatabase(s);
    w.resize(1279, 780);
    w.show();
    auto &u = *w.ui();
    QSignalSpy updated(&w, &MainWindow::updated);
    w.start();
    CHECK(waitFor([&] { return updated.count() >= 1; }, 20000));

    // start-up: this station from the database, every pin, centred on home at zoom 2
    CHECK(u.headerlabel->text() == "Reports for: G4GCI  at: IO90GU");
    CHECK(u.countlabel->text().endsWith("updated: 14 RX, 25 TX"));   // Q0TEST: not a locator
    CHECK(u.Zoomlabel->text() == "2" && u.mapView->zoom() == 2);
    CHECK(u.liveCalltextBox->text() == "G4GCI");
    // centred on home's longitude; at zoom 2 the world is hardly taller than the window, so the view
    // stops at the top of the map (as Leaflet's maxBounds) and home is above the middle
    const Map::LatLon c = u.mapView->centre();
    CHECK(std::abs(c.lon + 1.458) < 0.01 && c.lat > 20 && c.lat < 50.8);
    CHECK(!w.findChild<LoadErrorLabel *>()->isVisible());

    // tiles (file:// tiles in the tests) and a screenshot to look at
    if (!qEnvironmentVariable("WSPRMAP_TILES").isEmpty())
        CHECK(waitFor([&] { return u.mapView->tilesLoaded() >= 8 && u.mapView->tilesPending() == 0; }, 10000));
    const QString shot = qEnvironmentVariable("WSPRMAP_SHOT");
    if (!shot.isEmpty())
        w.grab().save(shot);

    // tooltips: a pin is the box above its point
    const Map::Point vk = [&] {
        for (const auto &p : u.mapView->data().tx)
            if (p.label.startsWith("VK2ABC"))
                return p;
        return Map::Point();
    }();
    CHECK(vk.label == "VK2ABC (20)" || vk.label == "VK2ABC (40)");
    // where VK2ABC is on screen (the copy of the world nearest the middle)
    auto onScreen = [&](double lat, double lon) {
        const int z = u.mapView->zoom();
        const QPointF centre = Map::project(u.mapView->centre().lat, u.mapView->centre().lon, z);
        QPointF p = Map::project(lat, lon, z) - centre;
        const double ws = Map::worldSize(z);
        while (p.x() > ws / 2) p.rx() -= ws;
        while (p.x() < -ws / 2) p.rx() += ws;
        return (p + QPointF(u.mapView->width() / 2.0, u.mapView->height() / 2.0)).toPoint();
    };
    const QString homeTip = u.mapView->labelAt(onScreen(50.854, -1.458));
    CHECK(homeTip.contains("Home Station") && homeTip.contains("G4GCI"));
    CHECK(u.mapView->labelAt(onScreen(vk.lat, vk.lon) + QPoint(0, -8)) == vk.label);
    CHECK(u.mapView->labelAt(onScreen(vk.lat, vk.lon) + QPoint(30, 30)).isEmpty());

    // Config: call / locator read-only until "Allow edit"; a warning, red when it differs from the database
    auto inConfig = [&](const std::function<void(QDialog *)> &f) {
        QTimer::singleShot(200, [&, f] {
            auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            CHECK(d != nullptr);
            if (d)
                f(d);
        });
        u.configbutton->click();
    };
    inConfig([&](QDialog *d) {
        auto *call = d->findChild<QLineEdit *>("calltextBox");
        auto *allow = d->findChild<QCheckBox *>("alloweditcheckBox");
        auto *warn = d->findChild<QLabel *>("stationwarning");
        CHECK(call->isReadOnly() && call->text() == "G4GCI" && !allow->isChecked());
        CHECK(warn->text().contains("The database has G4GCI at IO90GU"));
        allow->setChecked(true);
        CHECK(!call->isReadOnly() && warn->text().contains("Warning:") && !warn->text().contains("DIFFERENT"));
        call->setText("M0XYZ");
        CHECK(warn->text().contains("DIFFERENT"));
        if (!qEnvironmentVariable("WSPRMAP_SHOT_CONFIG").isEmpty())
            d->grab().save(qEnvironmentVariable("WSPRMAP_SHOT_CONFIG"));
        d->accept();
    });
    CHECK(waitFor([&] { return !w.busy() && u.headerlabel->text().startsWith("Reports for: M0XYZ"); }, 20000));
    CHECK(Map::readStation(home.path()).call == "M0XYZ");
    inConfig([&](QDialog *d) {
        auto *allow = d->findChild<QCheckBox *>("alloweditcheckBox");
        CHECK(allow->isChecked() && d->findChild<QLineEdit *>("calltextBox")->text() == "M0XYZ");
        allow->setChecked(false);   // back to the database's
        CHECK(d->findChild<QLineEdit *>("calltextBox")->text() == "G4GCI");
        d->accept();
    });
    CHECK(waitFor([&] { return !w.busy() && u.headerlabel->text().startsWith("Reports for: G4GCI"); }, 20000));
    CHECK(Map::readStation(home.path()).call.isEmpty());

    // paths off and on: no new update needed
    const int before = updated.count();
    u.pathcheckBox->setChecked(false);
    CHECK(!u.mapView->data().showPaths && updated.count() == before && u.mapView->data().tx.size() == 25);
    u.pathcheckBox->setChecked(true);
    CHECK(u.mapView->data().showPaths);

    // Centre buttons and zoom
    u.europeButton->click();
    CHECK(u.mapView->zoom() == 3 && u.Zoomlabel->text() == "3");
    if (!qEnvironmentVariable("WSPRMAP_SHOT_EUROPE").isEmpty()) {
        waitFor([&] { return u.mapView->tilesPending() == 0; }, 10000);
        u.mapView->grab().save(qEnvironmentVariable("WSPRMAP_SHOT_EUROPE"));
    }
    CHECK(std::abs(u.mapView->centre().lat - 50) < 0.01 && std::abs(u.mapView->centre().lon - 10) < 0.01);
    u.mapView->setZoom(30);
    CHECK(u.mapView->zoom() == Map::kMaxZoom);
    u.mapView->setZoom(0);
    CHECK(u.mapView->zoom() == Map::kMinZoom);
    u.recentrebutton->click();
    CHECK(std::abs(u.mapView->centre().lon + 1.458) < 0.01 && u.mapView->zoom() == 2);

    // km / miles (C#: kmcheckBox_CheckedChanged), and the de-clutter value in the request
    u.clutterlistBox->setCurrentRow(1);
    CHECK(std::abs(w.currentRequest().minKm - 50 * 1.60934) < 1e-6);
    u.kmcheckBox->setChecked(true);
    CHECK(u.mlslabel->text() == "km" && u.clutterlistBox->item(1)->text() == "80" && u.clutterlistBox->currentRow() == 0);
    u.clutterlistBox->setCurrentRow(1);
    CHECK(std::abs(w.currentRequest().minKm - 80) < 1e-6);
    u.clutterlistBox->setCurrentRow(0);

    // Apply with RX only and 20 m
    u.radioButton3->setChecked(true);
    u.bandlistBox->setCurrentRow(9);
    u.filterbutton->click();
    CHECK(w.busy());
    u.filterbutton->click();   // while busy: "Please wait until current map updated", nothing else
    CHECK(waitFor([&] { return !w.busy(); }, 20000));
    CHECK(u.mapView->data().tx.isEmpty() && u.mapView->data().rx.size() == 14);
    CHECK(u.mapView->data().rx[0].label.indexOf('(') < 0);   // one band: no "(20)"
    u.radioButton1->setChecked(true);
    u.bandlistBox->setCurrentRow(0);

    // auto track (C#: autocheckBox_CheckedChanged): period back to 3 hours, paths on
    u.periodlistBox->setCurrentRow(0);
    u.autocheckBox->setChecked(false);
    u.autocheckBox->setChecked(true);
    CHECK(u.periodlistBox->currentRow() == 5 && u.pathcheckBox->isChecked());

    // wspr.live mode
    QThread th;
    FakeLive fake;
    fake.moveToThread(&th);
    th.start();
    QMetaObject::invokeMethod(&fake, [&] { fake.start(); }, Qt::BlockingQueuedConnection);
    qputenv("WSPRMAP_LIVE_URL", QString("http://127.0.0.1:%1/").arg(fake.port).toLatin1());
    u.livecheckBox->setChecked(true);
    CHECK(u.Livepanel->isVisible());
    u.liveCalltextBox->setText("W1AW");
    const int n = updated.count();
    u.livebutton->click();
    CHECK(waitFor([&] { return updated.count() > n; }, 20000));
    CHECK(u.headerlabel->text() == "Reports for: W1AW  at: FN31");
    CHECK(u.mapView->data().tx.size() == 2 && u.mapView->data().rx.isEmpty());
    // back to the local map
    const int m = updated.count();
    u.livecheckBox->setChecked(false);
    CHECK(waitFor([&] { return updated.count() > m; }, 20000));
    CHECK(u.headerlabel->text() == "Reports for: G4GCI  at: IO90GU" && u.mapView->data().tx.size() == 25);
    qunsetenv("WSPRMAP_LIVE_URL");
    th.quit();
    th.wait();

    // the database goes away: the load error label, and it comes back with Apply
    Map::DbSettings down = s;
    down.port = 1;
    w.setDatabase(down);
    const int k = updated.count();
    u.filterbutton->click();
    CHECK(waitFor([&] { return updated.count() > k; }, 20000));
    auto *err = w.findChild<LoadErrorLabel *>();
    CHECK(err->isVisible() && err->QLabel::text().contains("MySQL server is not running") && err->QLabel::text().contains("press Apply"));
    CHECK(u.countlabel->text().endsWith("database not available"));
    if (!qEnvironmentVariable("WSPRMAP_SHOT_DOWN").isEmpty())
        w.grab().save(qEnvironmentVariable("WSPRMAP_SHOT_DOWN"));
    w.setDatabase(s);
    const int j = updated.count();
    u.filterbutton->click();
    CHECK(waitFor([&] { return updated.count() > j; }, 20000));
    CHECK(!err->isVisible() && u.mapView->data().tx.size() == 25);

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", "cleanup");
        db.setHostName(s.host);
        db.setUserName(s.user);
        db.setPassword(s.password);
        if (db.open()) {
            exec(db, "DROP DATABASE wspr_map_test_rpt");
            exec(db, "DROP DATABASE wspr_map_test_rx");
        }
    }
    QSqlDatabase::removeDatabase("cleanup");
    std::printf(failures ? "mapwindow_uitest: %d FAILED\n" : "mapwindow_uitest: all passed\n", failures);
    return failures ? 1 : 0;
}
