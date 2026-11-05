#pragma once

// The setup portal: how the device gets WiFi and an API key, and how it recovers when a
// stored network stops working.
//
// The portal itself (HTTP server, form, AP) lives in wifi_portal.cpp / wifi_prov.cpp.
// This module owns the policy around it: when to raise it, how long to wait, and what
// to do once credentials arrive. All four call sites that used to spawn the task by
// hand go through provisioning_begin().
void provisioning_begin();

// The portal task is running (or about to be).
bool provisioning_active();

// Called once after wifi_start(): raises the form if there is no stored network, or if
// the stored one did not connect.
void provisioning_after_wifi(bool online);

// Called from the main loop: reopens the form after a sustained dropout, and on a BOOT
// long press.
void provisioning_tick();
