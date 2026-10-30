Kraftonic Studio @VERSION@ - VST3 and CLAP plug-ins for Linux (@ARCH@)

Included plug-ins:
@PRODUCTS@

Install for the current user (~/.vst3 and ~/.clap):
    ./install.sh

Install for all users (/usr/lib/vst3 and /usr/lib/clap):
    sudo ./install.sh --system

Uninstall:
    ./install.sh --uninstall            (or: sudo ./install.sh --uninstall --system)

Manual install: copy the *.vst3 folders into ~/.vst3 or /usr/lib/vst3, and the
*.clap files into ~/.clap or /usr/lib/clap.
Then rescan plug-ins in your host (REAPER, Bitwig Studio, Ardour, ...).

Runtime libraries: the plug-in editor uses X11/XCB, Cairo, Pango and fontconfig,
which every desktop distribution already has. The CLAP plug-ins have no editor
on Linux yet: use the VST3 for the panel.

See LICENSE for the licence terms.
