CUPS driver for Konica Minolta magicolor 2300W, 2400W and 2500W printers
========================================================================

This provides rastertom2x00w filter and PPD files (specified by m2x00w.drv file) which
allows these printers to print from Linux and possibly any other OS where CUPS is used.

m2x00w-decode is a debug tool - it decodes 2x00W data (created either by rastertom2x00w
filter or windows drivers), producing a PBM bitmap and debug output.

This driver should work with these Minolta winprinters:

Printer type (IEEE1284 ID)	| Status
--------------------------------|--------------------------------------------------------
magicolor 2300W			| should work (older MINOLTA-QMS firmware)
mc2300W				| should work
magicolor 2400W			| should work
mc2500W				| works

Compiling from source
---------------------
Requirements: make, gcc, libcups2-dev, libcupsimage2-dev, cups-ppdc

To compile, simply run "make":

    $ make

To install compiled filter and drv file, run "make install" as root:

    # make install

or

    $ sudo make install

You can then install the printer using standard GUI tools or CUPS web interface.


Problems with CUPS libusb backend
---------------------------------
The libusb backend used by CUPS since 1.4.x is crap. The code is full of quirks for
various printers and it's no surprise that it does not work properly with M2x00W printers
too - the first document prints but nothing more is printed until the printer is turned off
and on again.

The solution is to set printer URI to the usblp device, e.g. "file:///dev/usb/lp0".
For this to work, file: device URIs must be enabled in CUPS configuration:
(/etc/cups/cups-files.conf)

    FileDevice Yes


Multiple USB printers
---------------------

If you have multiple USB printers, the usblp devices might be assigned differently on each boot or hot-plug. To avoid this, you can create an udev rule like this (example from 2500W):

    SUBSYSTEMS=="usb", ATTRS{ieee1284_id}=="MFG:KONICA MINOLTA;CMD:KMPS;MDL:mc2500W;CLS:PRINTER;DES:KONICA MINOLTA mc2500W;", SYMLINK+="m2500w"


You can find your printer's IEEE1284 ID by running:

    udevadm info -a --name=/dev/usb/lpX

When you are done restart udev:

    sudo service udev restart

Ensure that /dev/m2500w points to /dev/usb/lpX

Now you can map your printer as file:///dev/m2500w
