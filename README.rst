.. |build| image:: http://build.cloudef.pw/build/loliwm/master/linux%20x86_64/current/build-status.png 
.. _build: http://build.cloudef.pw/build/loliwm/master/linux%20x86_64

.. image:: http://cloudef.pw/armpit/loliwm-gh.png
:IRC: #loliwm @ freenode (temporary)
:Video: https://www.youtube.com/watch?v=nh_7aqNtrik
:Build: |build|_

OPTIONS
-------

Basic information about what you can currently do in loliwm.

+-----------------------+------------------------------------------------+
| ``--prefix MODIFIER`` | Set the modifier(s) to use with the keybinds.  |
|                       | shift, caps, ctrl, alt, mod2, mod3 and mod5    |
+-----------------------+------------------------------------------------+
| ``--log FILE``        | Logs output to specified ``FILE``.             |
+-----------------------+------------------------------------------------+

wlc specific env variables


+------------------+------------------------------------------------------+
| ``WLC_SHM``      | Set 1 to force EGL clients to use shared memory.     |
+------------------+------------------------------------------------------+
| ``WLC_OUTPUTS``  | Number of fake outputs in X11 mode.                  |
+------------------+------------------------------------------------------+
| ``WLC_BG``       | Set 0 to disable the background GLSL shader.         |
+------------------+------------------------------------------------------+
| ``WLC_XWAYLAND`` | Set 0 to disable Xwayland.                           |
+------------------+------------------------------------------------------+

KEYBINDS
--------

Note that these keybinds are temporary until configuration is added.

+-----------------+------------------------------------------------------+
| ``mod-return``  | Opens a terminal emulator.                           |
+-----------------+------------------------------------------------------+
| ``mod-p``       | Opens ``bemenu-run``.                                |
+-----------------+------------------------------------------------------+
| ``mod-l``       | Rotates focus through outputs.                       |
+-----------------+------------------------------------------------------+
| ``mod-j, k``    | Rotates focus through clients.                       |
+-----------------+------------------------------------------------------+
| ``mod-f``       | Toggles fullscreen.                                  |
+-----------------+------------------------------------------------------+
| ``mod-[1..n]``  | Activate space.                                      |
+-----------------+------------------------------------------------------+
| ``mod-F1..F10`` | Moves focused client to corresponding space.         |
+-----------------+------------------------------------------------------+
| ``mod-z, x, c`` | Moves focused client to output 1, 2 and 3            |
|                 | respectively.                                        |
+-----------------+------------------------------------------------------+
| ``mod-h``       | Cycles clients.                                      |
+-----------------+------------------------------------------------------+
| ``mod-q``       | Closes focused client.                               |
+-----------------+------------------------------------------------------+
| ``mod-i, o``    | Shifts the cut of the nmaster layout to shrink or    |
|                 | expand the view.                                     |
+-----------------+------------------------------------------------------+
| ``mod-print``   | Takes a screenshot in PPM (Portable Pixmap) format.  |
+-----------------+------------------------------------------------------+
| ``mod-esc``     | Quits ``loliwm``.                                    |
+-----------------+------------------------------------------------------+

KEYBOARD LAYOUT
---------------

You can set your preferred keyboard layout using ``XKB_DEFAULT_LAYOUT``.

.. code:: sh

    XKB_DEFAULT_LAYOUT=gb loliwm

RUNNING ON TTY
--------------

Running on TTY works right now.
You need to suid the loliwm binary to whichever group or user has rights to /dev/input.
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
Instructions later...
