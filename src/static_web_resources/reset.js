document.addEventListener("DOMContentLoaded", () => {
    const statusElement = document.getElementById("reset-status");
    const deviceButton = document.getElementById("reset-device");
    const configButton = document.getElementById("reset-config");

    function setStatus(message, isError = false) {
        if (!statusElement) {
            return;
        }

        statusElement.textContent = message;
        statusElement.style.color = isError ? "#b00020" : "";
    }

    async function postReset(endpoint, confirmMessage, successMessage) {
        if (!window.confirm(confirmMessage)) {
            return;
        }

        try {
            setStatus("Sending request...");
            const response = await fetch(endpoint, { method: "POST" });

            if (!response.ok) {
                const errorText = await response.text();
                throw new Error(`HTTP ${response.status}: ${errorText || "Request failed"}`);
            }

            setStatus(successMessage);
        } catch (error) {
            console.error(`Reset request to ${endpoint} failed:`, error);
            setStatus(`Failed: ${error.message}`, true);
        }
    }

    if (deviceButton) {
        deviceButton.addEventListener("click", () => {
            postReset(
                "/api/reset/device",
                "Restart the device now?",
                "Device reboot accepted. The connection may drop shortly."
            );
        });
    }

    if (configButton) {
        configButton.addEventListener("click", () => {
            postReset(
                "/api/reset/config",
                "Erase all settings, restore defaults, and restart the device?",
                "Configuration reset accepted. The device will reboot after clearing settings."
            );
        });
    }

    const backupButton = document.getElementById("backup-download");
    if (backupButton) {
        backupButton.addEventListener("click", async () => {
            try {
                setStatus("Preparing backup...");
                const response = await fetch("/api/settings/backup");
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}`);
                }
                const blob = await response.blob();
                const url = URL.createObjectURL(blob);
                const a = document.createElement("a");
                a.href = url;
                a.download = "settings.dat";
                document.body.appendChild(a);
                a.click();
                a.remove();
                URL.revokeObjectURL(url);
                setStatus("Backup downloaded.");
            } catch (error) {
                setStatus(`Backup failed: ${error.message}`, true);
            }
        });
    }

    const restoreButton = document.getElementById("restore-upload");
    const restoreFile = document.getElementById("restore-file");
    if (restoreButton && restoreFile) {
        restoreButton.addEventListener("click", async () => {
            if (!restoreFile.files.length) {
                setStatus("Choose a settings.dat file first.", true);
                return;
            }
            if (!window.confirm("Overwrite the current configuration with this file and reboot?")) {
                return;
            }
            try {
                setStatus("Uploading and applying...");
                const response = await fetch("/api/settings/restore", {
                    method: "POST",
                    headers: { "Content-Type": "application/octet-stream" },
                    body: restoreFile.files[0]
                });
                if (!response.ok) {
                    const text = await response.text();
                    throw new Error(`HTTP ${response.status}: ${text || "Request failed"}`);
                }
                setStatus("Configuration restored. The device is rebooting; reconnect shortly.");
            } catch (error) {
                setStatus(`Restore failed: ${error.message}`, true);
            }
        });
    }
});
