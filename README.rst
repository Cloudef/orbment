.. image:: http://cloudef.pw/armpit/loliwm-mpv.png

LOLIWM
______

Basic information about what you can currently do in loliwm.

KEYBINDS
--------

+----------------+-------------------------------+
| ``alt-return`` | Opens ``weston-terminal``     |
+----------------+-------------------------------+
| ``alt-l``      | Rotates focus through clients |
+----------------+-------------------------------+
| ``alt-q``      | Closes focused client         |
+----------------+-------------------------------+

KEYBOARD LAYOUT
---------------

You can set your prefered keyboard layout using ``XKB_DEFAULT_LAYOUT``.

.. code:: sh

    XKB_DEFAULT_LAYOUT=gb loliwm

RUNNING ON TTY
--------------

Running on TTY works right now.
However wlc does not yet set TTY to non interactive mode, so you may get stuck with some fancy ANSI escape.
(Similarly you can also quit loliwm with ``ctrl-c``)

You also need to suid the loliwm binary to whichever group or user has rights to /dev/input.
This is so wlc can spawn child process at start that gives rights for libinput to read from these raw input devices.

BUILDING
--------

You will need following makedepends:

- cmake
- git

And the following depends:

- pixman
- wayland (most likely from git)
- libxkbcommon
- udev
- libinput

You will also need these for building, but they are optional runtime:

- libx11
- libxcb
- mesa, nvidia, etc.. (GLESv2, EGL, DRM)

For weston-terminal and other wayland clients for testing, you might also want to build weston from git.

You can build bootstrapped version of ``loliwm`` with the following steps.

.. code:: sh

    git submodule update --init --recursive # - initialize and fetch submodules
    mkdir target && cd target               # - create build target directory
    cmake ..                                # - run CMake
    make                                    # - compile

    # You can now run
    ./src/loliwm

For proper packaging ``wlc`` and ``loliwm`` should be built separately.
...instructions later...
