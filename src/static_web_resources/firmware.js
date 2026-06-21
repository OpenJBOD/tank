document.addEventListener("DOMContentLoaded", () => {
    const fileInput = document.getElementById("fw-file");
    const uploadButton = document.getElementById("fw-upload");
    const progress = document.getElementById("fw-progress");
    const statusElement = document.getElementById("fw-status");

    function setStatus(message, isError = false) {
        statusElement.textContent = message;
        statusElement.style.color = isError ? "#b00020" : "";
    }

    async function loadVersions() {
        try {
            const response = await fetch("/api/device_info");
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            const json = await response.json();
            document.getElementById("sw-version").textContent = json.version || "Unknown";
            const bl = json.bootloader_version;
            document.getElementById("bootloader-version").textContent =
                (bl === undefined || bl === null) ? "Unknown" : `v${bl}`;
        } catch (error) {
            document.getElementById("bootloader-version").textContent = "Unavailable";
        }
    }

    /* Poll the device until it answers again after the post-upgrade reboot. */
    function waitForReconnect(attempt = 0) {
        if (attempt > 60) {
            setStatus("Device did not come back automatically - reload the page manually.", true);
            return;
        }
        setStatus(`Rebooting into the new firmware... reconnecting (${attempt}s)`);
        setTimeout(() => {
            fetch("/api/device_info", { cache: "no-store" })
                .then((r) => {
                    if (r.ok) {
                        setStatus("Device is back online. Reloading...");
                        setTimeout(() => window.location.reload(), 1000);
                    } else {
                        waitForReconnect(attempt + 1);
                    }
                })
                .catch(() => waitForReconnect(attempt + 1));
        }, 1000);
    }

    function upload() {
        const file = fileInput.files[0];
        if (!file) {
            setStatus("Choose a firmware image first.", true);
            return;
        }

        if (!window.confirm(`Upload ${file.name} (${file.size} bytes) and reboot into it?`)) {
            return;
        }

        /* XHR gives upload progress events; fetch does not. */
        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/api/firmware");
        xhr.setRequestHeader("Content-Type", "application/octet-stream");

        progress.style.display = "block";
        uploadButton.disabled = true;

        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                progress.value = Math.round((e.loaded / e.total) * 100);
                setStatus(`Uploading... ${progress.value}%`);
            }
        };

        xhr.onload = () => {
            uploadButton.disabled = false;
            if (xhr.status === 200) {
                progress.value = 100;
                waitForReconnect(0);
            } else {
                setStatus(`Upload failed: HTTP ${xhr.status} ${xhr.responseText}`, true);
            }
        };

        xhr.onerror = () => {
            uploadButton.disabled = false;
            setStatus("Upload failed: network error", true);
        };

        setStatus("Uploading...");
        xhr.send(file);
    }

    uploadButton.addEventListener("click", upload);
    loadVersions();
});
