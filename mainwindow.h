#pragma once
// WSPR Scheduler Map 3's window (C#: Wspr_Map Form1). The layout is mainwindow.ui (edit it in
// Qt Designer); the map itself is MapWidget.
#include "mapcore.h"

#include <QMainWindow>
#include <memory>

namespace Ui { class MainWindow; }
class LoadErrorLabel;
class QLabel;
class QThread;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // home folder for ~/DBmapcredential (tests); call before start()
    void setHomeDir(const QString &dir) { m_home = dir; }
    // database settings instead of 127.0.0.1 / admin / ~/DBmapcredential (tests: scratch databases)
    void setDatabase(const Map::DbSettings &db)
    {
        m_db = db;
        m_dbFixed = true;
    }
    void start();                        // C#: Form1_Load
    bool busy() const { return m_gettingData; }
    Map::Request currentRequest() const; // what Apply would ask for now
    Ui::MainWindow *ui() const { return m_ui.get(); }

    static constexpr int kAutoTrackMs = 600000;   // C#: timer1.Interval (10 minutes)

public slots:
    void applyClicked();                 // C#: filterbutton_Click / filter_button_action

signals:
    void updated();                      // a map update has finished (tests)

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    enum class Why { Start, Apply, Auto };
    void runUpdate(Why why);             // C#: filter_results, in a worker thread
    void finished(const Map::Result &res, Why why);
    void setHeader(const QString &call, const QString &loc);
    void showWait(const QString &text);
    void hideWait();
    void openConfig();                   // C#: configbutton_Click / groupBox1
    void kmChanged(bool km);
    void autoChanged(bool on);
    void liveChanged(bool on);
    void recentre();
    void placeOverlays();
    QString readPassword();

    std::unique_ptr<Ui::MainWindow> m_ui;
    QString m_home;
    Map::DbSettings m_db;
    QString m_call, m_locator;           // this station (local databases)
    QString m_dbCall, m_dbLocator;       // as the database has it (empty = no reports yet)
    QString m_prevCall;
    Map::Station m_manual;               // typed in Config (~/WSPRmapstation); empty = from the database
    bool m_dbFixed = false;
    bool m_gettingData = false;
    bool m_indexesChecked = false;
    QThread *m_worker = nullptr;
    QTimer *m_timer = nullptr;           // C#: timer1 (auto track)
    QLabel *m_wait = nullptr;            // C#: panel1 / Waitlabel
    QLabel *m_loading = nullptr;         // C#: loadingPanel / loadinglabel
    LoadErrorLabel *m_loadError = nullptr;
};
