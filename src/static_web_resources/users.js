/*
 * Copyright (c) 2024 OpenJBOD Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// Display a status message to the user
function showStatusMessage(message, isError = false) {
    const statusDiv = document.getElementById("status-messages");
    const messageDiv = document.createElement("div");
    messageDiv.style.padding = "10px";
    messageDiv.style.marginBottom = "10px";
    messageDiv.style.borderRadius = "4px";
    messageDiv.style.backgroundColor = isError ? "#ffebee" : "#e8f5e8";
    messageDiv.style.color = isError ? "#c62828" : "#2e7d32";
    messageDiv.style.border = isError ? "1px solid #e57373" : "1px solid #81c784";
    messageDiv.textContent = message;
    
    // Clear previous messages and show new one
    statusDiv.innerHTML = "";
    statusDiv.appendChild(messageDiv);
    
    // Auto-remove after 5 seconds
    setTimeout(() => {
        if (statusDiv.contains(messageDiv)) {
            statusDiv.removeChild(messageDiv);
        }
    }, 5000);
}

// Fetch and display current users
async function fetchUsers() {
    try {
        const response = await fetch("/api/users");
        if (!response.ok) {
            throw new Error(`Response status: ${response.status}`);
        }

        const users = await response.json();
        displayUsers(users);
        updateUserSelect(users);
        
    } catch (error) {
        console.error("Failed to fetch users:", error.message);
        showStatusMessage("Failed to load users: " + error.message, true);
        document.getElementById("users-list").innerHTML = "<p>Error loading users.</p>";
    }
}

// Display users in the list
function displayUsers(users) {
    const usersList = document.getElementById("users-list");
    
    if (!users || Object.keys(users).length === 0) {
        usersList.innerHTML = "<p>No users configured.</p>";
        return;
    }
    
    let html = "<table style='width: 100%; border-collapse: collapse;'>";
    html += "<tr style='background-color: #f5f5f5;'>";
    html += "<th style='padding: 8px; text-align: left; border: 1px solid #ddd;'>Username</th>";
    html += "<th style='padding: 8px; text-align: center; border: 1px solid #ddd;'>Actions</th>";
    html += "</tr>";
    
    for (const [key, user] of Object.entries(users)) {
        if (user.username && user.username.trim() !== "") {
            html += "<tr>";
            html += `<td style='padding: 8px; border: 1px solid #ddd;'>${user.username}</td>`;
            html += `<td style='padding: 8px; text-align: center; border: 1px solid #ddd;'>`;
            html += `<button onclick="deleteUser('${user.username}')" style='background-color: #f44336; color: white; border: none; padding: 4px 8px; cursor: pointer; border-radius: 3px;'>Delete</button>`;
            html += `</td>`;
            html += "</tr>";
        }
    }
    
    html += "</table>";
    usersList.innerHTML = html;
}

// Update the user select dropdown for password changes
function updateUserSelect(users) {
    const select = document.getElementById("change_user_select");
    
    // Clear existing options except the first one
    select.innerHTML = '<option value="">-- Select User --</option>';
    
    // Add users to the select
    for (const [key, user] of Object.entries(users)) {
        if (user.username && user.username.trim() !== "") {
            const option = document.createElement("option");
            option.value = user.username;
            option.textContent = user.username;
            select.appendChild(option);
        }
    }
}

// Add a new user
async function addUser(formData) {
    try {
        const requestData = {
            action: "create",
            username: formData.username,
            password: formData.password
        };
        
        const response = await fetch("/api/users", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify(requestData)
        });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`Response status: ${response.status} - ${errorText}`);
        }
        
        const result = await response.json();
        
        if (result.status === "success") {
            showStatusMessage("User added successfully!");
            fetchUsers(); // Refresh the user list
            
            // Clear the form
            document.getElementById("add-user-form").reset();
        } else {
            throw new Error(result.message || "Unknown error occurred");
        }
        
    } catch (error) {
        console.error("Failed to add user:", error.message);
        showStatusMessage("Failed to add user: " + error.message, true);
    }
}

// Change user password
async function changePassword(formData) {
    try {
        const requestData = {
            action: "update",
            username: formData.username,
            password: formData.password
        };
        
        const response = await fetch("/api/users", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify(requestData)
        });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`Response status: ${response.status} - ${errorText}`);
        }
        
        const result = await response.json();
        
        if (result.status === "success") {
            showStatusMessage("Password changed successfully!");
            
            // Clear the form
            document.getElementById("change-password-form").reset();
        } else {
            throw new Error(result.message || "Unknown error occurred");
        }
        
    } catch (error) {
        console.error("Failed to change password:", error.message);
        showStatusMessage("Failed to change password: " + error.message, true);
    }
}

// Delete a user
async function deleteUser(username) {
    if (!confirm(`Are you sure you want to delete user "${username}"? This action cannot be undone.`)) {
        return;
    }
    
    try {
        const requestData = {
            action: "delete",
            username: username
        };
        
        const response = await fetch("/api/users", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
            },
            body: JSON.stringify(requestData)
        });
        
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error(`Response status: ${response.status} - ${errorText}`);
        }
        
        const result = await response.json();
        
        if (result.status === "success") {
            showStatusMessage("User deleted successfully!");
            fetchUsers(); // Refresh the user list
        } else {
            throw new Error(result.message || "Unknown error occurred");
        }
        
    } catch (error) {
        console.error("Failed to delete user:", error.message);
        showStatusMessage("Failed to delete user: " + error.message, true);
    }
}

// Validate form inputs
function validateAddUserForm(formData) {
    if (!formData.username || formData.username.trim() === "") {
        showStatusMessage("Username is required.", true);
        return false;
    }
    
    if (formData.username.length > 31) {
        showStatusMessage("Username must be 31 characters or less.", true);
        return false;
    }
    
    if (!formData.password || formData.password === "") {
        showStatusMessage("Password is required.", true);
        return false;
    }

    if (formData.password.length < 8 || formData.password.length > 64) {
        showStatusMessage("Password must be 8-64 characters.", true);
        return false;
    }

    if (formData.password !== formData.confirm_password) {
        showStatusMessage("Passwords do not match.", true);
        return false;
    }

    return true;
}

function validateChangePasswordForm(formData) {
    if (!formData.username || formData.username === "") {
        showStatusMessage("Please select a user.", true);
        return false;
    }
    
    if (!formData.password || formData.password === "") {
        showStatusMessage("New password is required.", true);
        return false;
    }

    if (formData.password.length < 8 || formData.password.length > 64) {
        showStatusMessage("Password must be 8-64 characters.", true);
        return false;
    }

    if (formData.password !== formData.confirm_password) {
        showStatusMessage("Passwords do not match.", true);
        return false;
    }

    return true;
}

window.addEventListener("DOMContentLoaded", (ev) => {
    // Load current users on page load
    fetchUsers();
    
    // Add user form handler
    const addUserForm = document.getElementById("add-user-form");
    addUserForm.addEventListener("submit", (event) => {
        event.preventDefault();
        
        const formData = {
            username: document.getElementById("new_username").value.trim(),
            password: document.getElementById("new_password").value,
            confirm_password: document.getElementById("confirm_password").value
        };
        
        if (validateAddUserForm(formData)) {
            addUser(formData);
        }
    });
    
    // Change password form handler
    const changePasswordForm = document.getElementById("change-password-form");
    changePasswordForm.addEventListener("submit", (event) => {
        event.preventDefault();
        
        const formData = {
            username: document.getElementById("change_user_select").value,
            password: document.getElementById("change_password").value,
            confirm_password: document.getElementById("change_confirm_password").value
        };
        
        if (validateChangePasswordForm(formData)) {
            changePassword(formData);
        }
    });
});