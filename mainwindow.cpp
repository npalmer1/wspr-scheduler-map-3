// MainWindow - see mainwindow.h.
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "credentials.h"
#include "loaderror.h"
#include "msg.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#ifndef WSPRMAP_VERSION
#define WSPRMAP_VERSION "0"
#endif

namespace {
const char *kCredentialFile = "DBmapcredential";   // C#: ~/DBmapcredential ("db_user: receiver db_pass: ...")

QLabel *overlay(QWidget *parent, const QString &name, const QString &text)
{
    auto *l = new QLabel(text, parent);
    l->setObjectName(name);
    l->setAlignment(Qt::AlignCenter);
    l->setWordWrap(true);
    l->setStyleSheet("QLabel { background-color: seashell; color: black; border: 1px solid #c8b8a8; padding: 14px; }");
    l->setFixedSize(310, 120);
    l->hide();
    return l;
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_ui(new Ui::MainWindow), m_timer(new QTimer(this))
{
    m_ui->setupUi(this);
    auto &u = *m_ui;
    setWindowTitle(QString("WSPR Scheduler Map 3   V.%1   GNU GPLv3").arg(WSPRMAP_VERSION));
    u.Livepanel->hide();

    // C#: Form1_Load defaults
    u.bottomlabel->setText("...You must run WSPR Scheduler to display TX reports and WSPR Scheduler Live for RX reports");
    u.countlabel->setText("...");
    u.periodlistBox->setCurrentRow(5);   // 3 hours
    u.bandlistBox->setCurrentRow(0);     // all
    u.clutterlistBox->setCurrentRow(0);
    u.radioButton1->setChecked(true);
    u.pathcheckBox->setChecked(true);
    u.Zoomlabel->setText(QString::number(u.mapView->zoom()));

    m_wait = overlay(u.mapView, "waitpanel", "Please wait .. updating map");
    m_loading = overlay(u.mapView, "loadingpanel",
                        "Initialising WSPR Scheduler Map... please wait\n\n"
                        "Run WSPR Scheduler for RX reports +\nWSPR Scheduler Live for TX reports");
    m_loadError = new LoadErrorLabel("WSPR Scheduler Map", u.mapView);

    m_timer->setInterval(kAutoTrackMs);
    connect(m_timer, &QTimer::timeout, this, [this] {   // C#: timer1_Tick
        if (m_ui->autocheckBox->isChecked() && !m_gettingData)
            runUpdate(Why::Auto);
    });

    connect(u.filterbutton, &QPushButton::clicked, this, &MainWindow::applyClicked);
    connect(u.livebutton, &QPushButton::clicked, this, &MainWindow::applyClicked);
    connect(u.liveCalltextBox, &QLineEdit::returnPressed, this, &MainWindow::applyClicked);
    connect(u.liveCalltextBox, &QLineEdit::textEdited, this, [this](const QString &t) {
        const int pos = m_ui->liveCalltextBox->cursorPosition();   // C#: CharacterCasing.Upper
        m_ui->liveCalltextBox->setText(t.toUpper());
        m_ui->liveCalltextBox->setCursorPosition(pos);
    });
    connect(u.configbutton, &QPushButton::clicked, this, &MainWindow::openConfig);
    connect(u.recentrebutton, &QPushButton::clicked, this, &MainWindow::recentre);
    connect(u.europeButton, &QPushButton::clicked, this, [this] { m_ui->mapView->centreOn(50, 10, 3); });
    connect(u.americasButton, &QPushButton::clicked, this, [this] { m_ui->mapView->centreOn(40, -90, 3); });
    connect(u.pacificButton, &QPushButton::clicked, this, [this] { m_ui->mapView->centreOn(20, 150, 3); });
    connect(u.kmcheckBox, &QCheckBox::toggled, this, &MainWindow::kmChanged);
    connect(u.autocheckBox, &QCheckBox::toggled, this, &MainWindow::autoChanged);
    connect(u.livecheckBox, &QCheckBox::toggled, this, &MainWindow::liveChanged);
    connect(u.pathcheckBox, &QCheckBox::toggled, this, [this](bool on) {
        // C# fetched everything again; the paths are already here, so just draw them or not
        Map::MapData d = m_ui->mapView->data();
        d.showPaths = on;
        m_ui->mapView->setData(d);
    });
    connect(u.mapView, &MapWidget::zoomChanged, this, [this](int z) { m_ui->Zoomlabel->setText(QString::number(z)); });
}

MainWindow::~MainWindow()
{
    if (m_worker) {   // closed during an update: let it finish (database reads / wspr.live time out)
        m_worker->wait();
        delete m_worker;
    }
}

QString MainWindow::readPassword()
{
    // C#: getUserandPassword - ~/DBmapcredential, else the default "wspr"
    if (const auto pw = Security::readDbPasswordFile(kCredentialFile, m_home))
        return *pw;
    return QStringLiteral("wspr");
}

void MainWindow::start()
{
    if (!m_dbFixed)
        m_db.password = readPassword();
    m_manual = Map::readStation(m_home);
    m_loading->show();
    m_loading->raise();
    placeOverlays();
    runUpdate(Why::Start);
    if (m_ui->autocheckBox->isChecked())   // C#: Auto track is ticked in the Designer
        m_timer->start();
}

// ---- the update ----------------------------------------------------------------------------------

Map::Request MainWindow::currentRequest() const
{
    const auto &u = *m_ui;
    Map::Request r;
    r.live = u.livecheckBox->isChecked();
    r.liveCall = u.liveCalltextBox->text().trimmed().toUpper();
    r.periodIndex = u.periodlistBox->currentRow();
    r.bandIndex = qMax(0, u.bandlistBox->currentRow());
    if (u.clutterlistBox->currentRow() > 0) {
        const double v = u.clutterlistBox->currentItem()->text().toDouble();
        r.minKm = u.kmcheckBox->isChecked() ? v : Map::milesToKm(v);
    }
    r.hideSpecial = u.QcheckBox->isChecked();
    r.show = u.radioButton2->isChecked() ? Map::Show::TxOnly
             : u.radioButton3->isChecked() ? Map::Show::RxOnly
                                           : Map::Show::Both;
    r.showPaths = u.pathcheckBox->isChecked();
    // Config's call / locator when given, else the ones found in the database (empty = look them up)
    r.homeCall = m_manual.call.isEmpty() ? m_call : m_manual.call;
    r.homeLocator = m_manual.locator.isEmpty() ? m_locator : m_manual.locator;
    return r;
}

void MainWindow::applyClicked()
{
    if (m_gettingData) {
        Msg::timed(this, "Please wait until current map updated", "Map updating", 2500);
        return;
    }
    if (m_ui->livecheckBox->isChecked() && m_ui->liveCalltextBox->text().trimmed().isEmpty()) {
        Msg::timed(this, "Callsign empty", "Callsign", 2500);
        return;
    }
    runUpdate(Why::Apply);
}

void MainWindow::runUpdate(Why why)
{
    if (m_gettingData)
        return;
    m_gettingData = true;
    if (why != Why::Start)
        showWait(m_ui->livecheckBox->isChecked() ? "Please wait .. asking wspr.live" : "Please wait .. updating map");
    const Map::Request req = currentRequest();
    const Map::DbSettings db = m_db;
    const bool checkIndexes = !m_indexesChecked && !req.live;
    auto result = std::make_shared<Map::Result>();
    m_worker = QThread::create([req, db, checkIndexes, result] {
        if (checkIndexes)
            Map::ensureIndexes(db);   // C#: EnsureIndexes at start-up
        *result = Map::buildMap(req, db);
    });
    connect(m_worker, &QThread::finished, this, [this, result, why, checkIndexes] {
        m_worker->deleteLater();
        m_worker = nullptr;
        if (checkIndexes && !result->databaseDown)
            m_indexesChecked = true;
        finished(*result, why);
    });
    m_worker->start();
}

void MainWindow::finished(const Map::Result &res, Why why)
{
    // C#: SendMapData and the end of filter_results
    m_gettingData = false;
    hideWait();
    m_loading->hide();
    auto &u = *m_ui;
    const bool live = u.livecheckBox->isChecked();

    if (res.databaseDown && !live) {
        m_loadError->showText(LoadErrorLabel::text("WSPR Scheduler Map", LoadErrorLabel::kindFor(res.error))
                              + "\n\nMySQL said: " + res.error.trimmed().left(200)
                              + (u.autocheckBox->isChecked()
                                     ? "\n\nThe map tries again at the next auto-track update, or press Apply."
                                     : "\n\nPress Apply to try again."));
        placeOverlays();
        u.countlabel->setText(QDateTime::currentDateTimeUtc().toString("HH:mm:ss") + " UTC  database not available");
        emit updated();
        return;
    }
    m_loadError->hide();

    if (!live && !res.databaseDown) {
        m_dbCall = res.dbCall;
        m_dbLocator = res.dbLocator;
    }
    if (!live && res.data.hasHome) {   // remember this station for the next updates
        if (m_manual.call.isEmpty())   // keep the database's own, so clearing Config goes back to them
            m_call = res.data.homeCall;
        if (m_manual.locator.isEmpty())
            m_locator = res.data.homeLocator;
        if (m_prevCall.isEmpty())
            m_prevCall = m_call;
        if (u.liveCalltextBox->text().isEmpty())
            u.liveCalltextBox->setText(m_call);   // C#: liveCalltextBox.Text = call
    }
    if (res.data.hasHome)
        setHeader(res.data.homeCall, res.data.homeLocator);

    u.mapView->setData(res.data);
    // C#: centreOn(home, zoom 2) after every update. Auto track only does so when the map hasn't been
    // zoomed in, so it doesn't jump away from where you are looking.
    if (res.data.hasHome && (why != Why::Auto || u.mapView->zoom() <= Map::kMinZoom))
        u.mapView->centreOn(res.data.home.lat, res.data.home.lon, Map::kMinZoom);

    u.countlabel->setText(QString("%1 UTC  updated: %2 RX, %3 TX")
                              .arg(QDateTime::currentDateTimeUtc().toString("HH:mm:ss"))
                              .arg(res.data.rx.size())
                              .arg(res.data.tx.size()));
    if (!res.error.isEmpty())
        Msg::timed(this, res.error, live ? "wspr.live" : "WSPR Map", 4000);
    else if (!res.notice.isEmpty())
        Msg::timed(this, res.notice, live ? "wspr.live" : "WSPR Map", 3000);
    emit updated();
}

void MainWindow::setHeader(const QString &call, const QString &loc)
{
    QString s = "Reports for: " + call;
    if (!loc.isEmpty())
        s += "  at: " + loc;
    m_ui->headerlabel->setText(s);
}

// ---- controls ------------------------------------------------------------------------------------

void MainWindow::recentre()
{
    const Map::MapData &d = m_ui->mapView->data();
    if (d.hasHome)
        m_ui->mapView->centreOn(d.home.lat, d.home.lon, Map::kMinZoom);
}

void MainWindow::kmChanged(bool km)
{
    // C#: kmcheckBox_CheckedChanged
    m_ui->mlslabel->setText(km ? "km" : "mls");
    m_ui->clutterlistBox->clear();
    m_ui->clutterlistBox->addItems(Map::declutterSteps(km));
    m_ui->clutterlistBox->setCurrentRow(0);
}

void MainWindow::autoChanged(bool on)
{
    // C#: autocheckBox_CheckedChanged
    if (on) {
        m_ui->periodlistBox->setCurrentRow(5);   // 3 hours
        m_ui->pathcheckBox->setChecked(true);
        m_timer->start();
    } else {
        m_timer->stop();
    }
}

void MainWindow::liveChanged(bool on)
{
    // C#: livecheckBox_CheckedChanged
    m_ui->Livepanel->setVisible(on);
    if (on) {
        if (m_ui->liveCalltextBox->text().trimmed().isEmpty())
            Msg::timed(this, "Callsign empty", "Callsign", 2500);
        m_ui->liveCalltextBox->setFocus();
    } else {
        setHeader(m_prevCall.isEmpty() ? m_call : m_prevCall, m_locator);
        if (m_gettingData)
            return;   // the running update ends with the live map; the next one is local
        runUpdate(Why::Apply);
    }
}

void MainWindow::openConfig()
{
    // C#: configbutton_Click - groupBox1 with the station (from the database) and the password
    QDialog d(this);
    d.setWindowTitle("Config");
    d.setObjectName("configdialog");
    d.setStyleSheet("QDialog#configdialog { background-color: lightyellow; }");
    auto *lay = new QVBoxLayout(&d);
    auto *form = new QFormLayout;
    // C#: read-only, from the database. Here they can also be typed in (needed before there are any
    // reports), but only with "Allow edit" ticked, and with a warning: they must be this station's
    const bool manual = !m_manual.call.isEmpty() || !m_manual.locator.isEmpty();
    auto *call = new QLineEdit(manual ? m_manual.call : m_dbCall, &d);
    call->setObjectName("calltextBox");
    call->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9/]{0,15}"), call));
    auto *loc = new QLineEdit(manual ? m_manual.locator : m_dbLocator, &d);
    loc->setObjectName("locatortextBox");
    loc->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[A-Ra-r]{0,2}|[A-Ra-r]{2}[0-9]{1,2}|[A-Ra-r]{2}[0-9]{2}[A-Xa-x]{1,2}|"
                           "[A-Ra-r]{2}[0-9]{2}[A-Xa-x]{2}[0-9]{1,2}"), loc));
    auto *allow = new QCheckBox("Allow edit (use this callsign and locator, not the database's)", &d);
    allow->setObjectName("alloweditcheckBox");
    allow->setChecked(manual);
    auto *warn = new QLabel(&d);
    warn->setObjectName("stationwarning");
    warn->setWordWrap(true);
    warn->setTextFormat(Qt::RichText);
    const QString dbText = m_dbCall.isEmpty() ? QString("The database has no received reports yet.")
                                              : "The database has " + m_dbCall + " at " + m_dbLocator + ".";
    auto fit = [warn, &d] {   // the warning's height changes with its text: make room for it
        if (d.layout())
            d.layout()->activate();
        warn->setMinimumHeight(warn->heightForWidth(warn->width() > 50 ? warn->width() : 440) + 6);
        d.adjustSize();
    };
    auto refresh = [=] {
        const bool on = allow->isChecked();
        call->setReadOnly(!on);
        loc->setReadOnly(!on);
        const QString bg = on ? "white" : "#f0f0f0";
        call->setStyleSheet("QLineEdit { background: " + bg + "; }");
        loc->setStyleSheet("QLineEdit { background: " + bg + "; }");
        if (!on) {
            warn->setText("From the newest report WSPR Scheduler received (wspr_rpt.received). " + dbText);
            warn->setStyleSheet("color: #555;");
            fit();
            return;
        }
        const QString c = call->text().trimmed().toUpper(), l = loc->text().trimmed().toUpper();
        const bool differs = !m_dbCall.isEmpty()
                             && ((!c.isEmpty() && c != m_dbCall.toUpper())
                                 || (!l.isEmpty() && !m_dbLocator.toUpper().startsWith(l.left(qMin(l.size(), m_dbLocator.size())))));
        QString t = "<span style=\"color:#a05000\"><b>Warning:</b> these must be the callsign and locator of the station "
                    "whose reports are in the database (WSPR Scheduler's), or the paths are drawn from the wrong "
                    "place. " + dbText.toHtmlEscaped() + "</span>";
        if (differs)
            t += "<br><span style=\"color:#b00000\"><b>What you have typed is DIFFERENT from the database.</b></span>";
        warn->setText(t);
        warn->setStyleSheet(QString());
        fit();
    };
    connect(allow, &QCheckBox::toggled, &d, [=](bool on) {
        if (!on) {   // back to the database's
            call->setText(m_dbCall);
            loc->setText(m_dbLocator);
        }
        refresh();
    });
    connect(call, &QLineEdit::textChanged, &d, refresh);
    connect(loc, &QLineEdit::textChanged, &d, refresh);
    auto *pass = new QLineEdit(m_db.password, &d);
    pass->setObjectName("passtextBox");
    pass->setEchoMode(QLineEdit::Password);
    auto *show = new QCheckBox("Show password", &d);
    connect(show, &QCheckBox::toggled, pass, [pass](bool on) {   // C#: showcheckBox_CheckedChanged
        pass->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    });
    form->addRow("Station callsign:", call);
    form->addRow("Station locator:", loc);
    form->addRow(QString(), allow);
    form->addRow("Database password:", pass);
    form->addRow(QString(), show);
    lay->addLayout(form);
    lay->addWidget(warn);
    d.setMinimumWidth(500);
    lay->setSizeConstraint(QLayout::SetMinimumSize);
    refresh();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &d);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    lay->addWidget(buttons);
    if (d.exec() != QDialog::Accepted)
        return;
    Map::Station typed;   // unticked: none (the database's are used)
    if (allow->isChecked())
        typed = {call->text().trimmed().toUpper(), loc->text().trimmed().toUpper()};
    if (!typed.locator.isEmpty() && !Map::locatorToLatLon(typed.locator)) {
        Msg::timed(this, "\"" + typed.locator + "\" isn't a locator (4, 6 or 8 characters, e.g. IO90GU)", "Config", 3500);
        typed.locator.clear();
    }
    bool redo = false;
    if (typed.call != m_manual.call || typed.locator != m_manual.locator) {
        if (!Map::writeStation(typed, m_home))
            Msg::timed(this, "Unable to save the callsign and locator", "Config", 3000);
        m_manual = typed;
        redo = true;
    }
    const QString pw = pass->text().trimmed();
    if (!Security::writeDbCredentialFile(kCredentialFile, "receiver", pw, m_home))   // C#: saveUserandPassword
        Msg::timed(this, "Unable to save database credentials", "Config", 3000);
    if (pw != m_db.password) {   // C# saved it but kept using the old one until restarted
        m_db.password = pw;
        m_indexesChecked = false;
        redo = true;
    }
    if (redo && !m_gettingData)
        runUpdate(Why::Apply);
}

// ---- overlays ------------------------------------------------------------------------------------

void MainWindow::showWait(const QString &text)
{
    m_wait->setText(text);
    placeOverlays();
    m_wait->show();
    m_wait->raise();
}

void MainWindow::hideWait()
{
    m_wait->hide();
}

void MainWindow::placeOverlays()
{
    const QSize s = m_ui->mapView->size();
    m_wait->move((s.width() - m_wait->width()) / 2, 12);   // C#: panel1, top middle
    m_loading->move((s.width() - m_loading->width()) / 2, (s.height() - m_loading->height()) / 2);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    placeOverlays();
}
