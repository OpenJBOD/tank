/*
 * Copyright (c) 2025 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

function setStatus(message, isError = false) {
    const el = document.getElementById("console-status");
    el.textContent = message;
    el.style.color = isError ? "#b00020" : "";
}

async function fetchConsoleSettings() {
    try {
        const response = await fetch("/api/settings");
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }
        const json = await response.json();
        const c = json.console || {};
        // Default to enabled if the field is missing.
        document.getElementById("uart_enabled").checked = c.uart_enabled !== false;
        document.getElementById("usb_enabled").checked = c.usb_enabled !== false;
    } catch (error) {
        console.error("Failed to fetch console settings:", error.message);
        setStatus("Failed to load current settings.", true);
    }
}

async function saveConsoleSettings(uartEnabled, usbEnabled) {
    try {
        setStatus("Saving...");
        const response = await fetch("/api/settings", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                console: { uart_enabled: uartEnabled, usb_enabled: usbEnabled }
            })
        });
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`Response status: ${response.status} - ${errorText}`);
        }
        setStatus("Saved. Changes take effect on the next reboot.");
    } catch (error) {
        console.error("Failed to save console settings:", error.message);
        setStatus("Failed to save: " + error.message, true);
    }
}

window.addEventListener("DOMContentLoaded", () => {
    fetchConsoleSettings();

    const form = document.getElementById("console-form");
    form.addEventListener("submit", (event) => {
        event.preventDefault();

        const uartEnabled = document.getElementById("uart_enabled").checked;
        const usbEnabled = document.getElementById("usb_enabled").checked;

        if (!uartEnabled && !usbEnabled) {
            if (!window.confirm(
                "Disabling BOTH consoles leaves no serial shell at all (only the " +
                "network remains to manage the device). Continue?")) {
                return;
            }
        } else if (!uartEnabled) {
            if (!window.confirm(
                "The UART console is the primary recovery console. Disable it and " +
                "rely on the USB console / network? It takes effect on next reboot.")) {
                return;
            }
        }

        saveConsoleSettings(uartEnabled, usbEnabled);
    });
});
