# Settimino ESP-IDF diagnostic port

Upstream: https://github.com/davenardella/Settimino
Version: 2.1.0
Pinned commit: d2fa3aaddeb44fea6a59265723786d1c52c6ae7b
License: LGPL-3.0-or-later; see COPYING.LESSER and COPYING.

Settimino.cpp is unmodified upstream protocol code. Settimino.h changes only
its selected memory model from EXTENDED to SMALL, disabling unused helpers
and extended PLC-control methods. Original copyright notices are retained.

Platform.h replaces the Arduino platform selection with ESP-IDF types, timing,
and an lwIP Client interface. idf_client.cpp implements that interface with
network-interface binding, bounded connection/send/receive waits and complete
reads/writes. It logs nonzero S7 response header error codes because upstream
ReadArea reports a short error acknowledgement as errS7InvalidPDU (0x0100).
That library code is distinct from the PLC's own response error.

The app calls only ConnectTo, ReadArea (one byte of input image at IB0), and
Disconnect. The OP connection type, rack 0 and slot 1 match the earlier custom
probe. No PLC writes, CPU control, or access-setting changes are invoked.

This library uses shared global packet buffers and this integration runs one
client in one task. It is a diagnostic, not a general concurrent S7 service.
