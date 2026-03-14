/*
    SPDX-FileCopyrightText: 2006-2008 Robert Knight <robertknight@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// To time the creation and total launch time (i. e. until window is
// visible/responsive):
// #define PROFILE_STARTUP

// Own
#include "Application.h"
#include "KonsoleSettings.h"
#include "MainWindow.h"
#include "ViewManager.h"
#include "config-konsole.h"
#include "widgets/ViewContainer.h"

// OS specific
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QProxyStyle>
#include <QStandardPaths>
#include <qplatformdefs.h>

#ifdef Q_OS_LINUX
#include <QFile>
#include <QTextStream>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <thread>
#include <unistd.h>

static void crashHandler(int sig)
{
    // Async-signal-safe: only use write(), backtrace(), backtrace_symbols_fd()
    const char *msg = "\n=== KONSOLAI CRASH (signal ";
    write(STDERR_FILENO, msg, strlen(msg));
    // Write signal number
    char sigbuf[16];
    int len = 0;
    int s = sig;
    if (s == 0) {
        sigbuf[len++] = '0';
    } else {
        char tmp[16];
        int tl = 0;
        while (s > 0) {
            tmp[tl++] = '0' + (s % 10);
            s /= 10;
        }
        while (tl > 0)
            sigbuf[len++] = tmp[--tl];
    }
    write(STDERR_FILENO, sigbuf, len);
    const char *msg2 = ") ===\n";
    write(STDERR_FILENO, msg2, strlen(msg2));

    void *frames[64];
    int nframes = backtrace(frames, 64);
    backtrace_symbols_fd(frames, nframes, STDERR_FILENO);

    // Also write to a crash log file
    const char *logpath = "/tmp/konsolai-crash.log";
    int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        write(fd, sigbuf, len);
        write(fd, msg2, strlen(msg2));
        backtrace_symbols_fd(frames, nframes, fd);
        close(fd);
    }

    // Re-raise with default handler to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

// KDE
#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedString>
#include <KWindowSystem>

#if HAVE_DBUS
#include <KDBusService>
#include <QDBusConnection>
#include <QDBusMessage>
#endif

#define HAVE_STYLE_MANAGER __has_include(<KStyleManager>)
#if HAVE_STYLE_MANAGER
#include <KStyleManager>
#endif

using Konsole::Application;

#ifdef PROFILE_STARTUP
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#endif

// fill the KAboutData structure with information about contributors to Konsole.
void fillAboutData(KAboutData &aboutData);

// check and report whether this konsole instance should use a new konsole
// process, or reuse an existing konsole process.
bool shouldUseNewProcess(int argc, char *argv[]);

// restore sessions saved by KDE.
void restoreSession(Application &app);

#if HAVE_DBUS
// Workaround for a bug in KDBusService: https://bugs.kde.org/show_bug.cgi?id=355545
// It calls exit(), but the program can't exit before the QApplication is deleted:
// https://bugreports.qt.io/browse/QTBUG-48709
static bool needToDeleteQApplication = false;
void deleteQApplication()
{
    if (needToDeleteQApplication) {
        delete qApp;
    }
}
#endif

// This override resolves following problem: since some qt version if
// XDG_CURRENT_DESKTOP ≠ kde, then pressing and immediately releasing Alt
// key makes focus get stuck in QMenu.
// Upstream report: https://bugreports.qt.io/browse/QTBUG-77355
class MenuStyle : public QProxyStyle
{
public:
    MenuStyle(const QString &name)
        : QProxyStyle(name)
    {
    }

    int styleHint(const StyleHint stylehint, const QStyleOption *opt, const QWidget *widget, QStyleHintReturn *returnData) const override
    {
        return (stylehint == QStyle::SH_MenuBar_AltKeyNavigation) ? 0 : QProxyStyle::styleHint(stylehint, opt, widget, returnData);
    }
};

// Used to control migrating config entries.
// Increment when there are new keys to migrate.
static int CurrentConfigVersion = 1;

static void migrateRenamedConfigKeys()
{
    KSharedConfigPtr konsoleConfig = KSharedConfig::openConfig(QStringLiteral("konsolairc"));
    KConfigGroup verGroup = konsoleConfig->group(QStringLiteral("General"));
    const int savedVersion = verGroup.readEntry<int>("ConfigVersion", 0);
    if (savedVersion < CurrentConfigVersion) {
        struct KeyInfo {
            const char *groupName;
            const char *oldKeyName;
            const char *newKeyName;
        };

        static const KeyInfo keys[] = {{"KonsolaiWindow", "SaveGeometryOnExit", "RememberWindowSize"}};

        // Migrate renamed config keys
        for (const auto &[group, oldName, newName] : keys) {
            KConfigGroup cg = konsoleConfig->group(QLatin1String(group));
            if (cg.exists() && cg.hasKey(oldName)) {
                const bool value = cg.readEntry(oldName, false);
                cg.deleteEntry(oldName);
                cg.writeEntry(newName, value);
            }
        }

        // With 5.93 KColorSchemeManager from KConfigWidgets, handles the loading
        // and saving of the widget color scheme, and uses "ColorScheme" as the
        // entry name, so clean-up here
        KConfigGroup cg(konsoleConfig, QStringLiteral("UiSettings"));
        const QString schemeName = cg.readEntry("WindowColorScheme");
        cg.deleteEntry("WindowColorScheme");
        cg.writeEntry("ColorScheme", schemeName);

        verGroup.writeEntry("ConfigVersion", CurrentConfigVersion);
        konsoleConfig->sync();
    }
}

#ifdef Q_OS_LINUX
// Walk up a cgroup path from the given starting point, raising any memory.high
// limit below 1 GiB. Uses only raw POSIX I/O (no heap allocations) so it can
// run safely from a watchdog thread even when the cgroup is memory-throttled.
static bool fixCgroupMemoryHighRaw(const char *cgroupPath)
{
    bool fixed = false;

    char pathBuf[512];
    strncpy(pathBuf, cgroupPath, sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';

    while (strstr(pathBuf, "app.slice")) {
        char memHighPath[1024];
        snprintf(memHighPath, sizeof(memHighPath), "/sys/fs/cgroup%s/memory.high", pathBuf);

        int mfd = open(memHighPath, O_RDWR);
        if (mfd >= 0) {
            char val[64];
            ssize_t vn = read(mfd, val, sizeof(val) - 1);
            if (vn > 0) {
                val[vn] = '\0';
                // Trim trailing whitespace/newline
                while (vn > 0 && (val[vn - 1] == '\n' || val[vn - 1] == ' '))
                    val[--vn] = '\0';

                // Remove any limit that isn't "max" — terminal emulators
                // managing many sessions can easily exceed several GiB.
                if (strncmp(val, "max", 3) != 0) {
                    lseek(mfd, 0, SEEK_SET);
                    const char newVal[] = "max";
                    if (write(mfd, newVal, sizeof(newVal) - 1) > 0)
                        fixed = true;
                }
            }
            close(mfd);
        }

        char *lastSlash = strrchr(pathBuf, '/');
        if (!lastSlash || lastSlash == pathBuf)
            break;
        *lastSlash = '\0';
    }
    return fixed;
}

// Read /proc/self/cgroup and extract the cgroup path after "::".
// Returns the path in outBuf, or empty string on failure.
static void readSelfCgroupPath(char *outBuf, size_t outSize)
{
    outBuf[0] = '\0';
    int fd = open("/proc/self/cgroup", O_RDONLY);
    if (fd < 0)
        return;
    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';

    char *sep = strstr(buf, "::");
    if (!sep)
        return;
    char *path = sep + 2;
    char *nl = strchr(path, '\n');
    if (nl)
        *nl = '\0';

    strncpy(outBuf, path, outSize - 1);
    outBuf[outSize - 1] = '\0';
}

// Watchdog thread: retries for 30 seconds in case the app scope is created
// AFTER the process starts (which is the normal KDE Plasma behavior).
// Uses only raw POSIX syscalls — no heap allocations — so it keeps running
// even when the main thread is blocked in D state due to memory.high throttling.
static void cgroupMemoryWatchdog()
{
    for (int attempt = 0; attempt < 30; ++attempt) {
        usleep(1000000); // 1 second

        char cgpath[512];
        readSelfCgroupPath(cgpath, sizeof(cgpath));
        if (cgpath[0] == '\0')
            continue;

        if (fixCgroupMemoryHighRaw(cgpath))
            return; // Fixed, watchdog can exit
    }
}

// Qt wrapper for the delayed event-loop check (belt to the watchdog's suspenders).
static void ensureCgroupMemoryLimit()
{
    char cgpath[512];
    readSelfCgroupPath(cgpath, sizeof(cgpath));
    if (cgpath[0] != '\0')
        fixCgroupMemoryHighRaw(cgpath);
}
#endif

// ***
// Entry point into the Konsole terminal application.
// ***
int main(int argc, char *argv[])
{
#ifdef Q_OS_LINUX
    // Install crash handler to capture backtraces before KCrash/DrKonqi
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);

    // Early check (covers restarts where the scope already exists)
    ensureCgroupMemoryLimit();
    // Watchdog thread: KDE creates the app scope AFTER the process starts,
    // so the early check above runs before the scope/limit exist. The watchdog
    // retries every second for 30s using raw POSIX I/O (no heap allocations)
    // so it keeps running even if the main thread is D-state throttled.
    std::thread(cgroupMemoryWatchdog).detach();
#endif
#ifdef PROFILE_STARTUP
    QElapsedTimer timer;
    timer.start();
#endif

    /**
     * trigger initialisation of proper icon theme
     */
#if KICONTHEMES_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    KIconTheme::initTheme();
#endif

#if HAVE_DBUS
    // Check if any of the arguments makes it impossible to reuse an existing process.
    // We need to do this manually and before creating a QApplication, because
    // QApplication takes/removes the Qt specific arguments that are incompatible.
    const bool needNewProcess = shouldUseNewProcess(argc, argv);
    if (!needNewProcess) { // We need to avoid crashing
        needToDeleteQApplication = true;
    }
#endif

    auto app = new QApplication(argc, argv);

#if HAVE_STYLE_MANAGER
    /**
     * trigger initialisation of proper application style
     */
    KStyleManager::initStyle();
#else
    /**
     * For Windows and macOS: use Breeze if available
     * Of all tested styles that works the best for us
     */
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QApplication::setStyle(QStringLiteral("breeze"));
#endif
#endif

    // fix the alt key, ensure we keep the current selected style as base
    app->setStyle(new MenuStyle(app->style()->name()));

    migrateRenamedConfigKeys();

    app->setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));

    KLocalizedString::setApplicationDomain("konsolai");

    KAboutData about(QStringLiteral("konsolai"),
                     i18nc("@title", "Konsolai"),
                     QStringLiteral(KONSOLE_VERSION),
                     i18nc("@title", "Claude-native terminal emulator"),
                     KAboutLicense::GPL_V2,
                     i18nc("@info:credit", "(c) 2025 Struktured Labs\nBased on Konsole (c) 1997-2022, The Konsole Developers"),
                     QString(),
                     QStringLiteral("https://github.com/struktured-labs/konsolai"));
    fillAboutData(about);
    // Explicitly set the desktop filename to match the installed .desktop file.
    // KAboutData's constructor derives it from the homepage URL (github.com →
    // com.github.konsolai), but the desktop file is org.kde.konsolai.desktop.
    // Without this, KDE's app scope uses a wrong ID and applies default
    // (restrictive) cgroup memory limits instead of terminal-specific ones.
    about.setDesktopFileName(QStringLiteral("org.kde.konsolai"));

    KAboutData::setApplicationData(about);

    KCrash::initialize();

    QSharedPointer<QCommandLineParser> parser(new QCommandLineParser);
    parser->setApplicationDescription(about.shortDescription());
    about.setupCommandLine(parser.data());

    QStringList args = app->arguments();
    QStringList customCommand = Application::getCustomCommand(args);

    Application::populateCommandLineParser(parser.data());

    parser->process(args);
    about.processCommandLine(parser.data());

#if HAVE_DBUS
    // on wayland: init token if we are launched by Konsolai and have none
    if (KWindowSystem::isPlatformWayland() && qEnvironmentVariable("XDG_ACTIVATION_TOKEN").isEmpty() && QDBusConnection::sessionBus().interface()) {
        // can we ask Konsolai for a token?
        const auto konsoleService = qEnvironmentVariable("KONSOLAI_DBUS_SERVICE");
        const auto konsoleSession = qEnvironmentVariable("KONSOLAI_DBUS_SESSION");
        const auto konsoleActivationCookie = qEnvironmentVariable("KONSOLAI_DBUS_ACTIVATION_COOKIE");
        if (!konsoleService.isEmpty() && !konsoleSession.isEmpty() && !konsoleActivationCookie.isEmpty()) {
            // we ask the current shell session
            QDBusMessage m =
                QDBusMessage::createMethodCall(konsoleService, konsoleSession, QStringLiteral("org.kde.konsolai.Session"), QStringLiteral("activationToken"));

            // use the cookie from the environment
            m.setArguments({konsoleActivationCookie});

            // get the token, if possible and export it to environment for later use
            const auto tokenAnswer = QDBusConnection::sessionBus().call(m);
            if (tokenAnswer.type() == QDBusMessage::ReplyMessage && !tokenAnswer.arguments().isEmpty()) {
                const auto token = tokenAnswer.arguments().first().toString();
                if (!token.isEmpty()) {
                    qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
                }
            }
        }
    }

    /// ! DON'T TOUCH THIS ! ///
    const KDBusService::StartupOption startupOption =
        Konsole::KonsoleSettings::useSingleInstance() && !needNewProcess ? KDBusService::Unique : KDBusService::Multiple;
    /// ! DON'T TOUCH THIS ! ///
    // If you need to change something here, add your logic _at the bottom_ of
    // shouldUseNewProcess(), after reading the explanations there for why you
    // probably shouldn't.

    atexit(deleteQApplication);
    // Ensure that we only launch a new instance if we need to
    // If there is already an instance running, we will quit here
    KDBusService dbusService(startupOption | KDBusService::NoExitOnFailure);

    needToDeleteQApplication = false;
#endif

    // If we reach this location, there was no existing copy of Konsole
    // running, so create a new instance.
    Application konsoleApp(parser, customCommand);

#if HAVE_DBUS
    // The activateRequested() signal is emitted when a second instance
    // of Konsole is started.
    QObject::connect(&dbusService, &KDBusService::activateRequested, &konsoleApp, &Application::slotActivateRequested);
#endif

    if (app->isSessionRestored()) {
        restoreSession(konsoleApp);
    } else {
        // Do not finish starting Konsole due to:
        // 1. An argument was given to just printed info
        // 2. An invalid situation occurred
        const bool continueStarting = (konsoleApp.newInstance() != 0);
        if (!continueStarting) {
            delete app;
            return 0;
        }
    }

#ifdef PROFILE_STARTUP
    qDebug() << "Construction completed in" << timer.elapsed() << "ms";
    QTimer::singleShot(0, [&timer]() {
        qDebug() << "Startup complete in" << timer.elapsed() << "ms";
    });
#endif

    // Since we've allocated the QApplication on the heap for the KDBusService workaround,
    // we need to delete it manually before returning from main().
    int ret = app->exec();
    delete app;
    return ret;
}

bool shouldUseNewProcess(int argc, char *argv[])
{
    // The "unique process" model of konsole is incompatible with some or all
    // Qt/KDE options. When those incompatible options are given, konsole must
    // use new process
    //
    // TODO: make sure the existing list is OK and add more incompatible options.

    // We need to manually parse the arguments because QApplication removes the
    // Qt specific arguments (like --reverse)
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; i++) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }

    if (arguments.contains(QLatin1String("--force-reuse"))) {
        return false;
    }

    // take Qt options into consideration
    QStringList qtProblematicOptions;
    qtProblematicOptions << QStringLiteral("--session") << QStringLiteral("--name") << QStringLiteral("--reverse") << QStringLiteral("--stylesheet")
                         << QStringLiteral("--graphicssystem");
#if WITH_X11
    qtProblematicOptions << QStringLiteral("--display") << QStringLiteral("--visual");
#endif
    for (const QString &option : std::as_const(qtProblematicOptions)) {
        if (arguments.contains(option)) {
            return true;
        }
    }

    // take KDE options into consideration
    QStringList kdeProblematicOptions;
    kdeProblematicOptions << QStringLiteral("--config") << QStringLiteral("--style");
#if WITH_X11
    kdeProblematicOptions << QStringLiteral("--waitforwm");
#endif

    for (const QString &option : std::as_const(kdeProblematicOptions)) {
        if (arguments.contains(option)) {
            return true;
        }
    }

    // if users have explicitly requested starting a new process
    // Support --nofork to retain argument compatibility with older
    // versions.
    if (arguments.contains(QStringLiteral("--separate")) || arguments.contains(QStringLiteral("--nofork"))) {
        return true;
    }

    // the only way to create new tab is to reuse existing Konsole process.
    if (Konsole::KonsoleSettings::forceNewTabs() || arguments.contains(QStringLiteral("--new-tab"))) {
        return false;
    }

    // when starting Konsole from a terminal, a new process must be used
    // so that the current environment is propagated into the shells of the new
    // Konsole and any debug output or warnings from Konsole are written to
    // the current terminal
    bool hasControllingTTY = false;
    const int fd = QT_OPEN("/dev/tty", O_RDONLY);
    if (fd != -1) {
        hasControllingTTY = true;
        close(fd);
    }

    return hasControllingTTY;
}

void fillAboutData(KAboutData &aboutData)
{
    aboutData.setOrganizationDomain("kde.org");

    aboutData.addAuthor(i18nc("@info:credit", "struktured"),
                        i18nc("@info:credit", "Konsolai creator, Claude integration"),
                        QStringLiteral("struktured@strukturedlabs.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Kurt Hindenburg"),
                        i18nc("@info:credit",
                              "General maintainer, bug fixes and general"
                              " improvements"),
                        QStringLiteral("kurt.hindenburg@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Robert Knight"),
                        i18nc("@info:credit", "Previous maintainer, ported to KDE4"),
                        QStringLiteral("robertknight@gmail.com"));
    aboutData.addAuthor(i18nc("@info:credit", "Lars Doelle"), i18nc("@info:credit", "Original author"), QStringLiteral("lars.doelle@on-line.de"));
    aboutData.addCredit(i18nc("@info:credit", "Ahmad Samir"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("a.samirh78@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Carlos Alves"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("cbc.alves@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Tomaz Canabrava"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("tcanabrava@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Gustavo Carneiro"),
                        i18nc("@info:credit", "Major refactoring, bug fixes and major improvements"),
                        QStringLiteral("gcarneiroa@hotmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Edwin Pujols"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("edwin.pujols5@outlook.com"));
    aboutData.addCredit(i18nc("@info:credit", "Martin T. H. Sandsmark"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("martin.sandsmark@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Nate Graham"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("nate@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Mariusz Glebocki"),
                        i18nc("@info:credit", "Bug fixes and major improvements"),
                        QStringLiteral("mglb@arccos-1.net"));
    aboutData.addCredit(i18nc("@info:credit", "Thomas Surrel"),
                        i18nc("@info:credit", "Bug fixes and general improvements"),
                        QStringLiteral("thomas.surrel@protonmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Jekyll Wu"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("adaptee@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Waldo Bastian"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("bastian@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Stephan Binner"), i18nc("@info:credit", "Bug fixes and general improvements"), QStringLiteral("binner@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Thomas Dreibholz"), i18nc("@info:credit", "General improvements"), QStringLiteral("dreibh@iem.uni-due.de"));
    aboutData.addCredit(i18nc("@info:credit", "Chris Machemer"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("machey@ceinetworks.com"));
    aboutData.addCredit(i18nc("@info:credit", "Francesco Cecconi"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("francesco.cecconi@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Stephan Kulow"), i18nc("@info:credit", "Solaris support and history"), QStringLiteral("coolo@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Alexander Neundorf"),
                        i18nc("@info:credit", "Bug fixes and improved startup performance"),
                        QStringLiteral("neundorf@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Peter Silva"), i18nc("@info:credit", "Marking improvements"), QStringLiteral("Peter.A.Silva@gmail.com"));
    aboutData.addCredit(i18nc("@info:credit", "Lotzi Boloni"),
                        i18nc("@info:credit",
                              "Embedded Konsole\n"
                              "Toolbar and session names"),
                        QStringLiteral("boloni@cs.purdue.edu"));
    aboutData.addCredit(i18nc("@info:credit", "David Faure"),
                        i18nc("@info:credit",
                              "Embedded Konsole\n"
                              "General improvements"),
                        QStringLiteral("faure@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Antonio Larrosa"), i18nc("@info:credit", "Visual effects"), QStringLiteral("larrosa@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Matthias Ettrich"),
                        i18nc("@info:credit",
                              "Code from the kvt project\n"
                              "General improvements"),
                        QStringLiteral("ettrich@kde.org"));
    aboutData.addCredit(i18nc("@info:credit", "Warwick Allison"),
                        i18nc("@info:credit", "Schema and text selection improvements"),
                        QStringLiteral("warwick@troll.no"));
    aboutData.addCredit(i18nc("@info:credit", "Dan Pilone"), i18nc("@info:credit", "SGI port"), QStringLiteral("pilone@slac.com"));
    aboutData.addCredit(i18nc("@info:credit", "Kevin Street"), i18nc("@info:credit", "FreeBSD port"), QStringLiteral("street@iname.com"));
    aboutData.addCredit(i18nc("@info:credit", "Sven Fischer"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("herpes@kawo2.renditionwth-aachen.de"));
    aboutData.addCredit(i18nc("@info:credit", "Dale M. Flaven"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("dflaven@netport.com"));
    aboutData.addCredit(i18nc("@info:credit", "Martin Jones"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("mjones@powerup.com.au"));
    aboutData.addCredit(i18nc("@info:credit", "Lars Knoll"), i18nc("@info:credit", "Bug fixes"), QStringLiteral("knoll@mpi-hd.mpg.de"));
    aboutData.addCredit(i18nc("@info:credit", "Thanks to many others.\n"));
}

void restoreSession(Application &app)
{
    int n = 1;

    while (KMainWindow::canBeRestored(n)) {
        auto mainWindow = app.newMainWindow();
        mainWindow->restore(n++);
        mainWindow->viewManager()->toggleActionsBasedOnState();
        mainWindow->show();

        // TODO: HACK without the code below the sessions would be `uninitialized`
        // and the tabs wouldn't display the correct information.
        auto tabbedContainer = qobject_cast<Konsole::TabbedViewContainer *>(mainWindow->centralWidget());
        for (int i = 0; i < tabbedContainer->count(); i++) {
            tabbedContainer->setCurrentIndex(i);
        }
    }
}
