# WSPR Scheduler Map 3 on Linux

Tested the same way as WSPR Scheduler 2: Ubuntu 24.04 and Debian 13; Raspberry Pi OS (Debian 13
Trixie) should work the same.

## Install

1. Download the install script and run it, as your normal user (it asks for your password itself):

   ```
   wget https://raw.githubusercontent.com/npalmer1/wspr-scheduler-map-3/main/linux/install.sh
   bash install.sh
   ```

   It installs git if needed, downloads WSPR Scheduler Map 3 into `~/wspr-scheduler-map-3`, and installs it.
   (If you already have a copy cloned from GitHub, `bash linux/install.sh` in it does the same.)

2. Start **WSPR Scheduler Map 3** from the applications menu (or type `WsprMap`).

### What the script does

- Installs git if it isn't there, and gets the source: clones it into `~/wspr-scheduler-map-3` (another folder
  with `--dir`), or if it's already there, pulls the latest version.
- Installs what WSPR Scheduler Map 3 needs: Qt 6 and its MySQL driver, the database server, the Liberation fonts,
  and the compiler and CMake to build it. The database server is **MariaDB**, unless the PC already has
  MySQL, which is then kept.
- Builds WSPR Scheduler Map 3 and installs it in `/usr/local`, with a menu entry.
- Starts MySQL and sets it to start with the PC, adds the `admin` database user (password `wspr`) if
  it isn't there, and makes the WSPR Scheduler databases **only if they aren't there already**, from
  WSPR Scheduler 2's `wspr_db.sql` (its copy on this PC, or downloaded from its GitHub page).
  Existing databases are never changed.

If WSPR Scheduler 2 is already installed on the PC, the database part finds everything in place and
leaves it alone, so the order you install the programs in doesn't matter.

The database password is `wspr` unless you change it with Config (it's saved in `~/DBmapcredential`,
the same as on Windows).

Options: `--dir FOLDER` (keep the source somewhere else), `--no-update` (build the copy you have, as it
is), `--no-mysql` (the databases are on another PC), `--uninstall`.

## Update

Run the script again:

```
bash ~/wspr-scheduler-map-3/linux/install.sh
```

It pulls the latest version from GitHub, rebuilds and reinstalls the program; the databases and settings
are kept. If you have changed files in `~/wspr-scheduler-map-3` yourself, it stops and lists them, so nothing is lost:
`git stash` keeps them aside, `git checkout .` throws them away, or `--no-update` builds your copy as it is.

## Uninstall

```
bash ~/wspr-scheduler-map-3/linux/install.sh --uninstall
```

removes the program and its menu entry. The source folder, the databases and your settings stay.
