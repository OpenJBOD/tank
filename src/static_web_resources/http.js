document.addEventListener('DOMContentLoaded', function() {
    loadHTTPSettings();
    setupEventListeners();
});

function setupEventListeners() {
    const form = document.getElementById('http-form');
    const uploadButton = document.getElementById('upload_certs');
    const httpCheckbox = document.getElementById('enable_http');
    const httpsCheckbox = document.getElementById('enable_https');

    form.addEventListener('submit', handleFormSubmit);
    
    // Ensure at least one server is always enabled
    httpCheckbox.addEventListener('change', function() {
        if (!this.checked && !httpsCheckbox.checked) {
            httpsCheckbox.checked = true;
        }
    });
    
    httpsCheckbox.addEventListener('change', function() {
        if (!this.checked && !httpCheckbox.checked) {
            httpCheckbox.checked = true;
        }
    });
    
    uploadButton.addEventListener('click', uploadCertificates);
}

function loadHTTPSettings() {
    fetch('/api/settings')
        .then(response => response.json())
        .then(data => {
            if (data.http) {
                document.getElementById('enable_http').checked = data.http.enable_http;
                document.getElementById('enable_https').checked = data.http.enable_https;
                document.getElementById('http_port').value = data.http.http_port;
                document.getElementById('https_port').value = data.http.https_port;
                document.getElementById('use_custom_certificates').checked = data.http.use_custom_certificates;
            }
        })
        .catch(error => {
            console.error('Error loading HTTP settings:', error);
            alert('Failed to load HTTP settings');
        });
}

function handleFormSubmit(event) {
    event.preventDefault();
    
    const formData = new FormData(event.target);
    const settings = {
        http: {
            enable_http: formData.has('enable_http'),
            enable_https: formData.has('enable_https'),
            http_port: parseInt(formData.get('http_port')),
            https_port: parseInt(formData.get('https_port')),
            use_custom_certificates: formData.has('use_custom_certificates')
        }
    };
    
    fetch('/api/settings', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(settings)
    })
    .then(response => response.json())
    .then(data => {
        if (data.status === 'settings_updated' || data.status === 'settings_unchanged') {
            alert('HTTP settings saved successfully! The HTTP server will now restart. It will be available again in a few seconds.');
        } else {
            alert('Failed to save HTTP settings: ' + (data.error || data.message || 'Unknown error'));
        }
    })
    .catch(error => {
        console.error('Error saving HTTP settings:', error);
        alert('Failed to save HTTP settings');
    });
}

function uploadCertificates() {
    const certFile = document.getElementById('server_cert').files[0];
    const keyFile = document.getElementById('private_key').files[0];
    
    console.log('Certificate file:', certFile);
    console.log('Private key file:', keyFile);
    
    if (!certFile || !keyFile) {
        alert('Please select both certificate and private key files');
        return;
    }
    
    console.log('Certificate file name:', certFile.name, 'size:', certFile.size);
    console.log('Private key file name:', keyFile.name, 'size:', keyFile.size);
    
    const formData = new FormData();
    formData.append('certificate', certFile);
    formData.append('private_key', keyFile);
    
    console.log('FormData entries:');
    for (let [key, value] of formData.entries()) {
        console.log('Field:', key, 'Value:', value, 'Size:', value.size || 'N/A');
    }
    
    fetch('/api/certificates/upload', {
        method: 'POST',
        body: formData
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            alert('Certificates uploaded successfully! Enable "Use Custom Certificates" and restart the device to use the new certificates.');
        } else {
            alert('Failed to upload certificates: ' + (data.message || 'Unknown error'));
        }
    })
    .catch(error => {
        console.error('Error uploading certificates:', error);
        alert('Failed to upload certificates');
    });
}

