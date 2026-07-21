#include "crow.h"
extern "C" {
    #include "libcaptcha.h"
    #include "stb_image_write.h"
}
#include <unordered_map>
#include <random>
#include <mutex>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iostream>

struct Session {
    std::string status;       // "pending", "verified"
    std::string captcha_code; // current captcha string for this session
    std::string token;        // session token
};

std::unordered_map<std::string, Session> session_store; // user_id -> Session
std::unordered_map<std::string, std::string> token_to_user; // token -> user_id
std::mutex store_mutex;

// Simple random string generator for secure tokens / captcha codes
std::string generate_random_string(size_t length = 6, bool uppercase_only = false) {
    static const char charset[] = "23456789abcdefghjkmnpqrstuvwxyzABCDEFGHJKMNPQRSTUVWXYZ";
    static const char upper_charset[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    const char* selected_charset = uppercase_only ? upper_charset : charset;
    size_t charset_size = strlen(selected_charset);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(0, charset_size - 1);
    
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += selected_charset[dis(gen)];
    }
    return result;
}

std::string generate_uuid() {
    return generate_random_string(16);
}

// Callback for stb_write_png_to_func
static void png_write_func_cb(void *context, void *data, int size) {
    auto *buffer = static_cast<std::string*>(context);
    buffer->append(static_cast<const char*>(data), size);
}

std::string create_captcha_png(const std::string& text, const std::string& font_path) {
    if (!std::filesystem::exists(font_path)) {
        std::cerr << "Font file not found: " << font_path << std::endl;
        return "";
    }

    lc_fontBuffer *font = lc_create_font(font_path.c_str());
    if (!font) {
        std::cerr << "Failed to create font from: " << font_path << std::endl;
        return "";
    }

    lc_arrGlyph *arr = lc_str_to_arr(font, text.c_str(), 38, 0);
    if (!arr) {
        lc_free(font);
        return "";
    }

    /* Glyph position randomization for both x and y axis */
    lc_randomize_arr_x(arr, 30);
    lc_randomize_arr_y(arr, 40);

    lc_bmp *bmp = lc_arr_to_bmp(arr);
    if (!bmp) {
        lc_free(arr);
        lc_free(font);
        return "";
    }

    std::string png_data;
    stbi_write_png_to_func(png_write_func_cb, &png_data, bmp->w, bmp->h, bmp->ch, bmp->buffer, bmp->w * bmp->ch);

    lc_free(bmp);
    lc_free(arr);
    lc_free(font);
    return png_data;
}

int main() {
    std::setvbuf(stdout, NULL, _IONBF, 0);
    std::setvbuf(stderr, NULL, _IONBF, 0);
    crow::SimpleApp app;

    // STEP 2 & 3: Bot requests a secure link
    CROW_ROUTE(app, "/create-session/<string>").methods(crow::HTTPMethod::POST)([](std::string user_id){
        std::string token = generate_uuid();
        
        {
            std::lock_guard<std::mutex> lock(store_mutex);
            // Clean up old session for this user if exists
            if (session_store.count(user_id)) {
                std::string old_token = session_store[user_id].token;
                token_to_user.erase(old_token);
            }
            session_store[user_id] = Session{
                .status = "pending",
                .captcha_code = "",
                .token = token
            };
            token_to_user[token] = user_id;
        }

        // Return the secure, unique link back to the bot
        // Note: adjust host/port to point to public domain or local address as needed
        crow::json::wvalue response;
        response["url"] = "http://localhost:8080/captcha/" + token;
        response["token"] = token;
        return crow::response(response);
    });

    // Endpoint to serve CAPTCHA HTML page
    CROW_ROUTE(app, "/captcha/<string>").methods(crow::HTTPMethod::GET)([](std::string token){
        {
            std::lock_guard<std::mutex> lock(store_mutex);
            if (!token_to_user.count(token)) {
                return crow::response(404, "Invalid or expired session.");
            }
        }

        std::string html = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Verification Required</title>
    <style>
        :root {
            --bg-primary: #0f172a;
            --bg-secondary: #1e293b;
            --accent: #6366f1;
            --accent-hover: #4f46e5;
            --text-primary: #f8fafc;
            --text-secondary: #94a3b8;
            --border: #334155;
            --success: #10b981;
            --error: #ef4444;
        }
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: 'Outfit', 'Inter', -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
        }
        body {
            background-color: var(--bg-primary);
            color: var(--text-primary);
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            padding: 20px;
        }
        .container {
            background-color: var(--bg-secondary);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 40px;
            width: 100%;
            max-width: 440px;
            box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3), 0 8px 10px -6px rgba(0, 0, 0, 0.3);
            text-align: center;
            animation: fadeIn 0.4s ease-out;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: translateY(10px); }
            to { opacity: 1; transform: translateY(0); }
        }
        h1 {
            font-size: 1.5rem;
            margin-bottom: 8px;
            font-weight: 700;
            background: linear-gradient(135deg, #a5b4fc, #6366f1);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        p {
            color: var(--text-secondary);
            font-size: 0.9rem;
            margin-bottom: 24px;
        }
        .captcha-box {
            background-color: #0b0f19;
            border: 1px solid var(--border);
            border-radius: 12px;
            padding: 16px;
            margin-bottom: 20px;
            position: relative;
            overflow: hidden;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100px;
        }
        .captcha-img {
            max-width: 100%;
            height: auto;
            border-radius: 6px;
        }
        .refresh-btn {
            background: none;
            border: none;
            color: var(--text-secondary);
            cursor: pointer;
            font-size: 0.8rem;
            margin-top: 8px;
            display: inline-flex;
            align-items: center;
            gap: 4px;
            transition: color 0.2s;
        }
        .refresh-btn:hover {
            color: var(--accent);
        }
        input[type="text"] {
            width: 100%;
            background-color: var(--bg-primary);
            border: 1px solid var(--border);
            color: var(--text-primary);
            border-radius: 8px;
            padding: 12px 16px;
            font-size: 1rem;
            text-align: center;
            margin-bottom: 20px;
            letter-spacing: 2px;
            transition: border-color 0.2s, box-shadow 0.2s;
        }
        input[type="text"]:focus {
            outline: none;
            border-color: var(--accent);
            box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.2);
        }
        button.submit-btn {
            width: 100%;
            background-color: var(--accent);
            color: white;
            border: none;
            border-radius: 8px;
            padding: 12px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: background-color 0.2s, transform 0.1s;
        }
        button.submit-btn:hover {
            background-color: var(--accent-hover);
        }
        button.submit-btn:active {
            transform: scale(0.98);
        }
        .message {
            margin-top: 16px;
            font-size: 0.9rem;
            border-radius: 8px;
            padding: 10px;
            display: none;
        }
        .message.success {
            background-color: rgba(16, 185, 129, 0.15);
            color: var(--success);
            border: 1px solid rgba(16, 185, 129, 0.3);
            display: block;
        }
        .message.error {
            background-color: rgba(239, 68, 68, 0.15);
            color: var(--error);
            border: 1px solid rgba(239, 68, 68, 0.3);
            display: block;
        }
    </style>
</head>
<body>
    <div class="container" id="captcha-container">
        <h1>Human Verification</h1>
        <p>Please enter the characters you see in the image below.</p>
        
        <div class="captcha-box">
            <img id="captcha-img" class="captcha-img" src="/captcha-image/)html" + token + R"html(" alt="CAPTCHA">
        </div>
        <button type="button" class="refresh-btn" onclick="refreshCaptcha()">
            🔄 Refresh Image
        </button>

        <form id="captcha-form" onsubmit="submitCaptcha(event)" style="margin-top: 20px;">
            <input type="text" id="captcha-input" placeholder="Enter code" autocomplete="off" required>
            <button type="submit" class="submit-btn">Verify</button>
        </form>

        <div id="status-message" class="message"></div>
    </div>

    <script>
        const token = ")html" + token + R"html(";
        
        function refreshCaptcha() {
            const img = document.getElementById('captcha-img');
            img.src = '/captcha-image/' + token + '?t=' + Date.now();
            document.getElementById('captcha-input').value = '';
            const msgDiv = document.getElementById('status-message');
            msgDiv.style.display = 'none';
        }

        async function submitCaptcha(e) {
            e.preventDefault();
            const inputVal = document.getElementById('captcha-input').value;
            const msgDiv = document.getElementById('status-message');
            
            try {
                const response = await fetch('/verify-submit', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/x-www-form-urlencoded',
                    },
                    body: 'token=' + encodeURIComponent(token) + '&captcha_input=' + encodeURIComponent(inputVal)
                });
                
                const text = await response.text();
                if (response.ok) {
                    msgDiv.className = 'message success';
                    msgDiv.textContent = text;
                    msgDiv.style.display = 'block';
                    document.getElementById('captcha-form').style.display = 'none';
                    document.querySelector('.refresh-btn').style.display = 'none';
                } else {
                    msgDiv.className = 'message error';
                    msgDiv.textContent = text;
                    msgDiv.style.display = 'block';
                    refreshCaptcha();
                }
            } catch (err) {
                msgDiv.className = 'message error';
                msgDiv.textContent = 'Connection error. Please try again.';
                msgDiv.style.display = 'block';
            }
        }
    </script>
</body>
</html>
        )html";

        return crow::response(html);
    });

    // Endpoint to generate and serve the CAPTCHA image
    CROW_ROUTE(app, "/captcha-image/<string>").methods(crow::HTTPMethod::GET)([](std::string token){
        std::string code;
        {
            std::lock_guard<std::mutex> lock(store_mutex);
            if (!token_to_user.count(token)) {
                return crow::response(404, "Session not found");
            }
            std::string user_id = token_to_user[token];
            
            // Generate a new captcha code for this request
            code = generate_random_string(6, true);
            session_store[user_id].captcha_code = code;
        }

        // Generate the PNG image data
        std::string png_data = create_captcha_png(code, "www/ttf/dejavu.ttf");
        if (png_data.empty()) {
            return crow::response(500, "Error rendering captcha");
        }

        crow::response res;
        res.code = 200;
        res.set_header("Content-Type", "image/png");
        res.set_header("Cache-Control", "no-cache, no-store, must-revalidate");
        res.body = png_data;
        return res;
    });

    // STEP 5: User submits CAPTCHA here
    CROW_ROUTE(app, "/verify-submit").methods(crow::HTTPMethod::POST)([](const crow::request& req){
        std::string token;
        std::string captcha_input;

        auto url_params = crow::query_string(req.url);
        token = url_params.get("token") ? url_params.get("token") : "";
        captcha_input = url_params.get("captcha_input") ? url_params.get("captcha_input") : "";

        // Parse urlencoded post body if parameters were not in the query string
        if (token.empty() || captcha_input.empty()) {
            auto body_params = crow::query_string("?" + req.body);
            if (token.empty()) token = body_params.get("token") ? body_params.get("token") : "";
            if (captcha_input.empty()) captcha_input = body_params.get("captcha_input") ? body_params.get("captcha_input") : "";
        }

        bool verified = false;
        {
            std::lock_guard<std::mutex> lock(store_mutex);
            if (token_to_user.count(token)) {
                std::string user_id = token_to_user[token];
                std::string stored_code = session_store[user_id].captcha_code;
                
                std::string input_lower = captcha_input;
                std::string stored_lower = stored_code;
                std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(), ::tolower);
                std::transform(stored_lower.begin(), stored_lower.end(), stored_lower.begin(), ::tolower);

                if (!stored_code.empty() && input_lower == stored_lower) {
                    session_store[user_id].status = "verified";
                    // Delete code after successful verification ("delete after finish")
                    session_store[user_id].captcha_code = "";
                    verified = true;
                } else {
                    // Clear code on failure to force a new captcha on refresh/retry
                    session_store[user_id].captcha_code = "";
                }
            }
        }

        if (verified) {
            return crow::response(200, "Verification successful! You can return to Discord.");
        } else {
            return crow::response(400, "Incorrect code. Please try the new captcha.");
        }
    });

    // STEP 6: Bot polls this endpoint to check if user passed
    CROW_ROUTE(app, "/check-status/<string>").methods(crow::HTTPMethod::GET)([](std::string user_id){
        std::lock_guard<std::mutex> lock(store_mutex);
        if (session_store.count(user_id)) {
            return crow::response(200, session_store[user_id].status);
        }
        return crow::response(404, "Not Found");
    });

    app.port(8080).multithreaded().run();
}
