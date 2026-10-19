
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <SPI.h>
#include <SD.h>
#include "mbedtls/md.h"

// ================== SETTINGS (change these) ==================
#define SD_CS 5

const char* AP_SSID    = "YOUR_WIFI_NAME";
const char* AP_PASS    = "CHANGE_THIS_WIFI_PASSWORD"; // Replace locally; minimum 8 characters.

const char* ADMIN_USER = "CHANGE_ADMIN_USERNAME";
const char* ADMIN_PASS = "CHANGE_THIS_ADMIN_PASSWORD"; // Replace locally before running.

const unsigned long SESSION_MS = 30UL * 60UL * 1000UL;  // auto logout after 30 min
const int MAX_USERS = 30;
// =============================================================

const char* SHARED_DIR = "/shared";      // ONE common folder: every logged-in user sees the same files
const char* USERS_FILE = "/users.txt";   // line format: name,salt,hash,status   (status: P=pending, A=active)

struct Session {
  String token;
  String user;
  bool admin;
  unsigned long last;
};
const int MAX_SESSIONS = 12;
Session sessions[MAX_SESSIONS];

// Prototypes (needed because of the Session type)
Session* getSession();
Session* requireLogin();
String targetOwner(Session* s);
bool buildPath(Session* s, String& path, String& owner, String& name);
void sendFilesPage(Session* s);

const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);
File uploadFile;
bool uploadOK = false;
bool sdOK = false;

uint64_t cardTotal = 0, cardUsed = 0;
unsigned long lastCardCalc = 0;

// ================== CSS ==================
const char PAGE_CSS[] = R"rawliteral(
:root{--bg:#0f172a;--card:#1e293b;--accent:#38bdf8;--text:#f8fafc;--sub:#94a3b8;--border:#334155;--danger:#f43f5e;--ok:#34d399}
*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}
body{background:var(--bg);color:var(--text);padding:20px;display:flex;justify-content:center}
.app{width:100%;max-width:900px;display:flex;flex-direction:column;gap:20px}
.card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:20px;box-shadow:0 10px 15px -3px rgba(0,0,0,.3)}
.header{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}
.header h2{color:var(--accent);font-size:1.4rem}
.nav{display:flex;gap:10px;align-items:center;flex-wrap:wrap;font-size:.9rem}
.nav a{color:var(--accent);text-decoration:none}
.nav a:hover{text-decoration:underline}
.badge{background:rgba(56,189,248,.15);color:var(--accent);padding:4px 10px;border-radius:20px;font-size:.8rem;font-weight:600}
.note{margin-top:12px;color:var(--sub);font-size:.9rem}
.note a,.sub a{color:var(--accent)}
.bar-bg{background:var(--border);height:10px;border-radius:5px;overflow:hidden;margin:12px 0 6px}
.bar-fill{background:linear-gradient(90deg,var(--accent),var(--ok));height:100%}
.stext{display:flex;justify-content:space-between;font-size:.85rem;color:var(--sub);flex-wrap:wrap;gap:6px}
.drop{border:2px dashed var(--accent);border-radius:8px;padding:25px;text-align:center;background:rgba(56,189,248,.05);cursor:pointer}
.drop:hover{background:rgba(56,189,248,.1)}
.drop p{color:var(--sub)}
#fileInput{display:none}
.pw{display:none;margin-top:15px}
.pbar{background:var(--border);height:8px;border-radius:4px;overflow:hidden}
.pfill{background:var(--ok);height:100%;width:0%}
.search-input{width:100%;padding:10px 14px;background:var(--bg);border:1px solid var(--border);border-radius:8px;color:var(--text);outline:none;margin-bottom:12px}
.search-input:focus{border-color:var(--accent)}
table{width:100%;border-collapse:collapse;text-align:left;font-size:.95rem}
th{padding:12px;border-bottom:2px solid var(--border);color:var(--sub)}
td{padding:12px;border-bottom:1px solid var(--border);vertical-align:middle}
.fname{color:var(--accent);text-decoration:none;font-weight:500;word-break:break-all}
.fname:hover{text-decoration:underline}
.btn{padding:6px 12px;border-radius:6px;border:none;font-size:.82rem;font-weight:600;cursor:pointer;text-decoration:none;display:inline-block}
.btn-main{background:var(--accent);color:#0f172a;width:100%;padding:11px;font-size:1rem}
.btn-view{background:rgba(255,255,255,.1);color:var(--text)}
.btn-ok{background:rgba(52,211,153,.15);color:var(--ok);border:1px solid var(--ok)}
.btn-danger{background:rgba(244,63,94,.15);color:var(--danger);border:1px solid var(--danger)}
.btn-danger:hover{background:var(--danger);color:#fff}
.actions{display:flex;gap:6px;justify-content:flex-end;flex-wrap:wrap}
.actions form{display:inline}
.auth{max-width:380px;width:100%;margin:60px auto 0}
.auth h2{color:var(--accent);margin-bottom:6px}
.sub{color:var(--sub);font-size:.9rem;margin:10px 0}
.msg{padding:10px 12px;border-radius:8px;margin:12px 0;font-size:.9rem}
.msg.ok{background:rgba(52,211,153,.15);color:var(--ok)}
.msg.err{background:rgba(244,63,94,.15);color:var(--danger)}
.empty{color:var(--sub);padding:10px 0}
)rawliteral";

// ================== HELPERS ==================
String formatBytes(uint64_t bytes) {
  if (bytes < 1024) return String((unsigned long)bytes) + " B";
  else if (bytes < 1048576ULL) return String(bytes / 1024.0, 1) + " KB";
  else if (bytes < 1073741824ULL) return String(bytes / 1048576.0, 1) + " MB";
  else return String(bytes / 1073741824.0, 1) + " GB";
}

String htmlEscape(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else o += c;
  }
  return o;
}

String urlEncode(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    unsigned char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

String randomHex(int bytes) {
  String o;
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < bytes; i++) {
    uint8_t b = esp_random() & 0xFF;
    o += hex[b >> 4];
    o += hex[b & 0xF];
  }
  return o;
}

String sha256Hex(const String& in) {
  uint8_t out[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)in.c_str(), in.length());
  mbedtls_md_finish(&ctx, out);
  mbedtls_md_free(&ctx);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", out[i]);
  hex[64] = 0;
  return String(hex);
}

bool validUsername(const String& u) {
  if (u.length() < 3 || u.length() > 16) return false;
  if (u == ADMIN_USER) return false;
  for (size_t i = 0; i < u.length(); i++) {
    char c = u[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

String cleanFileName(String n) {
  int p = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (p >= 0) n = n.substring(p + 1);
  String o;
  for (size_t i = 0; i < n.length() && o.length() < 60; i++) {
    char c = n[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == ' ' || c == '(' || c == ')') o += c;
    else o += '_';
  }
  while (o.startsWith(".")) o = o.substring(1);
  o.trim();
  if (o.length() == 0) o = "file";
  return o;
}

void refreshCardInfo() {
  // NOTE: SD.usedBytes() scans the whole card and freezes the server for seconds,
  // so we only read the (instant) card size.
  if (cardTotal == 0) cardTotal = SD.cardSize();
}

void ensureShared() {
  if (!SD.exists(SHARED_DIR)) SD.mkdir(SHARED_DIR);
}

void redirect(const String& to) {
  server.sendHeader("Location", to);
  server.sendHeader("Cache-Control", "no-store");
  server.send(303, "text/plain", "");
}

String pageHead(const String& title) {
  String h = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  h += "<title>" + title + "</title><style>";
  h += PAGE_CSS;
  h += "</style></head><body><div class='app'>";
  return h;
}

// ================== USER STORE ==================
bool getUser(const String& name, String& salt, String& hash, char& status) {
  File f = SD.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  bool found = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int a = line.indexOf(',');
    int b = line.indexOf(',', a + 1);
    int c = line.indexOf(',', b + 1);
    if (a < 0 || b < 0 || c < 0) continue;
    if (line.substring(0, a) == name) {
      salt = line.substring(a + 1, b);
      hash = line.substring(b + 1, c);
      status = line.charAt(c + 1);
      found = true;
      break;
    }
  }
  f.close();
  return found;
}

// Returns "name1,name2," for users with the given status
String listUsers(char wanted) {
  String out;
  File f = SD.open(USERS_FILE, FILE_READ);
  if (!f) return out;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int a = line.indexOf(',');
    if (a < 0) continue;
    if (line.charAt(line.length() - 1) == wanted) out += line.substring(0, a) + ",";
  }
  f.close();
  return out;
}

int countUsers() {
  int n = 0;
  File f = SD.open(USERS_FILE, FILE_READ);
  if (!f) return 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length()) n++;
  }
  f.close();
  return n;
}

bool addUser(const String& name, const String& pass) {
  String salt = randomHex(4);
  String hash = sha256Hex(salt + pass);
  File f = SD.open(USERS_FILE, FILE_APPEND);
  if (!f) return false;
  f.println(name + "," + salt + "," + hash + ",P");
  f.close();
  return true;
}

// mode 'A' = approve, 'D' = delete
bool updateUser(const String& name, char mode) {
  File f = SD.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  String out;
  bool changed = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    int a = line.indexOf(',');
    if (a > 0 && line.substring(0, a) == name) {
      changed = true;
      if (mode == 'D') continue;
      if (mode == 'A') line.setCharAt(line.length() - 1, 'A');
    }
    out += line + "\n";
  }
  f.close();
  SD.remove(USERS_FILE);
  File w = SD.open(USERS_FILE, FILE_WRITE);
  if (!w) return false;
  w.print(out);
  w.close();
  return changed;
}

void removeUserFolder(const String& name) {
  String dir = "/users/" + name;
  File d = SD.open(dir);
  if (!d) return;
  String names;
  File f = d.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      String n = String(f.name());
      n = n.substring(n.lastIndexOf('/') + 1);
      names += n + "\n";
    }
    f = d.openNextFile();
  }
  d.close();
  int start = 0;
  while (true) {
    int i = names.indexOf('\n', start);
    if (i < 0) break;
    SD.remove(dir + "/" + names.substring(start, i));
    start = i + 1;
  }
  SD.rmdir(dir);
}

// ================== SESSIONS ==================
Session* getSession() {
  String cookie = server.header("Cookie");
  int p = cookie.indexOf("SID=");
  if (p < 0) return nullptr;
  String tok = cookie.substring(p + 4);
  int e = tok.indexOf(';');
  if (e >= 0) tok = tok.substring(0, e);
  tok.trim();
  if (tok.length() != 32) return nullptr;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token == tok) {
      if (millis() - sessions[i].last > SESSION_MS) {
        sessions[i].token = "";
        return nullptr;
      }
      sessions[i].last = millis();
      return &sessions[i];
    }
  }
  return nullptr;
}

String createSession(const String& user, bool admin) {
  int slot = 0;
  unsigned long oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token.length() == 0) { slot = i; break; }
    if (sessions[i].last < oldest) { oldest = sessions[i].last; slot = i; }
  }
  sessions[slot].token = randomHex(16);
  sessions[slot].user = user;
  sessions[slot].admin = admin;
  sessions[slot].last = millis();
  return sessions[slot].token;
}

void killSessionsOf(const String& user) {
  for (int i = 0; i < MAX_SESSIONS; i++)
    if (sessions[i].user == user) sessions[i].token = "";
}

void startSession(const String& user, bool admin) {
  String t = createSession(user, admin);
  server.sendHeader("Set-Cookie", "SID=" + t + "; Path=/; HttpOnly");
  redirect("/");
}

Session* requireLogin() {
  Session* s = getSession();
  if (!s) redirect("/login");
  return s;
}

// Which folder is being accessed? Users: always their own. Admin: may pass ?u=name
String targetOwner(Session* s) {
  return "shared";   // everyone uses the same shared folder
}

bool buildPath(Session* s, String& path, String& owner, String& name) {
  owner = targetOwner(s);
  name = server.arg("file");
  if (name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) return false;
  path = String(SHARED_DIR) + "/" + name;
  return true;
}

// ================== LOGIN / REGISTER ==================
void sendLoginPage(bool reg, const String& msg, bool good) {
  String h = pageHead(reg ? "Register" : "Login");
  h += "<div class='card auth'><h2>&#9889; ESP32 Drive</h2>";
  h += "<p class='sub'>" + String(reg ? "Create an account" : "Sign in to your drive") + "</p>";
  if (msg.length()) h += "<div class='msg " + String(good ? "ok" : "err") + "'>" + msg + "</div>";
  h += "<form method='POST' action='" + String(reg ? "/register" : "/login") + "'>";
  h += "<input class='search-input' name='username' placeholder='Username' autocomplete='username' autocapitalize='none' maxlength='16' required>";
  h += "<input class='search-input' type='password' name='password' placeholder='Password' maxlength='32' required>";
  h += "<button class='btn btn-main' type='submit'>" + String(reg ? "Register" : "Login") + "</button></form>";
  if (reg) {
    h += "<p class='sub'>Username: 3-16 letters, numbers or _<br>Password: 6 or more characters</p>";
    h += "<p class='sub'>Already have an account? <a href='/login'>Login</a></p>";
  } else {
    h += "<p class='sub'>New here? <a href='/register'>Register</a></p>";
  }
  h += "</div></div></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", h);
}

void handleLoginGet() {
  if (getSession()) return redirect("/");
  sendLoginPage(false, "", false);
}

void handleLoginPost() {
  String u = server.arg("username");
  u.trim();
  u.toLowerCase();
  String p = server.arg("password");

  if (u == ADMIN_USER && p == ADMIN_PASS) {
    ensureShared();
    return startSession(u, true);
  }
  if (!sdOK) return sendLoginPage(false, "SD card not available.", false);

  String salt, hash;
  char status;
  if (!getUser(u, salt, hash, status) || sha256Hex(salt + p) != hash) {
    delay(400);  // slow down guessing
    return sendLoginPage(false, "Wrong username or password.", false);
  }
  if (status == 'P') return sendLoginPage(false, "Your account is waiting for admin approval.", false);

  ensureShared();
  startSession(u, false);
}

void handleRegisterGet() {
  sendLoginPage(true, "", false);
}

void handleRegisterPost() {
  String u = server.arg("username");
  u.trim();
  u.toLowerCase();
  String p = server.arg("password");

  if (!sdOK) return sendLoginPage(true, "SD card not available.", false);
  if (!validUsername(u)) return sendLoginPage(true, "Invalid username. Use 3-16 letters, numbers or _ .", false);
  if (p.length() < 6 || p.length() > 32) return sendLoginPage(true, "Password must be 6 to 32 characters.", false);
  if (countUsers() >= MAX_USERS) return sendLoginPage(true, "User limit reached. Ask the admin.", false);

  String s, h;
  char st;
  if (getUser(u, s, h, st)) return sendLoginPage(true, "That username is already taken.", false);

  if (!addUser(u, p)) return sendLoginPage(true, "Could not save account.", false);
  sendLoginPage(false, "Registered! Wait for the admin to approve, then login.", true);
}

void handleLogout() {
  Session* s = getSession();
  if (s) s->token = "";
  server.sendHeader("Set-Cookie", "SID=; Path=/; Max-Age=0");
  redirect("/login");
}

// ================== FILES PAGE ==================
void sendFilesPage(Session* s) {
  if (!sdOK) {
    server.send(500, "text/plain", "SD card not available. Check wiring and card.");
    return;
  }
  String owner = targetOwner(s);
  bool other = false;
  ensureShared();
  String dir = SHARED_DIR;

  // pass 1: totals
  uint64_t folderBytes = 0;
  int count = 0;
  File d = SD.open(dir);
  if (d) {
    File f = d.openNextFile();
    while (f) {
      if (!f.isDirectory()) { folderBytes += f.size(); count++; }
      f = d.openNextFile();
    }
    d.close();
  }

  refreshCardInfo();
  int pct = (cardTotal > 0) ? (int)((folderBytes * 100) / cardTotal) : 0;
  if (pct < 1 && folderBytes > 0) pct = 1;

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  String h = pageHead("ESP32 Drive");
  h += "<div class='card'><div class='header'><h2>&#9889; ESP32 Drive</h2><div class='nav'>";
  h += "<span class='badge'>" + htmlEscape(s->user) + (s->admin ? " (admin)" : "") + "</span>";
  if (s->admin) {
    int pending = 0;
    String pl = listUsers('P');
    for (size_t i = 0; i < pl.length(); i++) if (pl[i] == ',') pending++;
    h += "<a href='/admin'>Admin panel" + String(pending ? " (" + String(pending) + " pending)" : "") + "</a>";
  }
  h += "<a href='/logout'>Logout</a></div></div>";
  if (other) h += "<p class='note'>Viewing files of <b>" + htmlEscape(owner) + "</b> &middot; <a href='/'>back to my files</a></p>";
  h += "<div class='bar-bg'><div class='bar-fill' style='width:" + String(pct) + "%'></div></div>";
  h += "<div class='stext'><span>Card size: <b>" + formatBytes(cardTotal) + "</b></span>";
  h += "<span>Shared files: <b>" + String(count) + " (" + formatBytes(folderBytes) + ")</b></span></div></div>";
  server.sendContent(h);

  if (!other) {
    server.sendContent(
      "<div class='card'><div class='drop' id='dropZone'>"
      "<p id='dzText'>&#128193; Drag &amp; drop files here, or</p>"
      "<label for='fileInput' class='btn btn-main' style='display:block;max-width:260px;margin:12px auto 0;text-align:center'>Choose files</label>"
      "<input type='file' id='fileInput' multiple onchange='uploadFiles(this.files)'></div>"
      "<div class='pw' id='pw'><div class='stext' style='margin-bottom:5px'><span id='ust'>Uploading...</span><span id='upc'>0%</span></div>"
      "<div class='pbar'><div class='pfill' id='pf'></div></div></div></div>");
  }

  server.sendContent(
    "<div class='card'><input type='text' id='q' class='search-input' placeholder='Search files...' onkeyup='filterFiles()'>"
    "<table id='ft'><thead><tr><th>Name</th><th>Size</th><th style='text-align:right'>Actions</th></tr></thead><tbody>");

  // pass 2: rows
  d = SD.open(dir);
  if (d) {
    File f = d.openNextFile();
    while (f) {
      if (!f.isDirectory()) {
        String name = String(f.name());
        name = name.substring(name.lastIndexOf('/') + 1);
        String enc = urlEncode(name);
        String q = "?file=" + enc + "&u=" + owner;
        String r = "<tr><td><a class='fname' href='/download" + q + "'>&#128196; " + htmlEscape(name) + "</a></td>";
        r += "<td>" + formatBytes(f.size()) + "</td><td><div class='actions'>";
        r += "<a class='btn btn-view' target='_blank' href='/view" + q + "'>View</a>";
        if (s->admin) {   // only the admin can delete
          r += "<form method='POST' action='/delete' onsubmit=\"return confirm('Delete this file?');\">";
          r += "<input type='hidden' name='file' value='" + htmlEscape(name) + "'>";
          r += "<button class='btn btn-danger' type='submit'>Delete</button></form>";
        }
        r += "</div></td></tr>";
        server.sendContent(r);
      }
      f = d.openNextFile();
    }
    d.close();
  }

  String tail = "</tbody></table>";
  if (count == 0) tail += "<p class='empty'>No files yet.</p>";
  tail += "</div></div>";
  tail += F("<script>"
    "function filterFiles(){var q=document.getElementById('q').value.toLowerCase();"
    "document.querySelectorAll('#ft tbody tr').forEach(function(r){r.style.display=r.children[0].innerText.toLowerCase().indexOf(q)>=0?'':'none';});}"
    "var dz=document.getElementById('dropZone');"
    "if(dz){['dragenter','dragover','dragleave','drop'].forEach(function(n){dz.addEventListener(n,function(e){e.preventDefault();e.stopPropagation();});});"
    "dz.addEventListener('drop',function(e){if(e.dataTransfer.files.length)uploadFiles(e.dataTransfer.files);});}"
    "if(window.matchMedia&&matchMedia('(hover: none)').matches){var t=document.getElementById('dzText');if(t)t.innerText='Tap the button to pick files from your phone';}"
    "function uploadFiles(l){var a=Array.prototype.slice.call(l);if(!a.length)return;var i=0;"
    "var pw=document.getElementById('pw'),pf=document.getElementById('pf'),upc=document.getElementById('upc'),ust=document.getElementById('ust');pw.style.display='block';"
    "function next(){if(i>=a.length){ust.innerText='All uploaded! Refreshing...';setTimeout(function(){location.href='/';},700);return;}"
    "var f=a[i];var fd=new FormData();fd.append('data',f);var x=new XMLHttpRequest();pf.style.width='0%';upc.innerText='0%';"
    "ust.innerText='Uploading '+f.name+' ('+(i+1)+'/'+a.length+')...';"
    "x.upload.addEventListener('progress',function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);pf.style.width=p+'%';upc.innerText=p+'%';}});"
    "x.onload=function(){if(x.status===200){i++;next();}else{ust.innerText='Upload failed: '+f.name;}};"
    "x.onerror=function(){ust.innerText='Upload failed: '+f.name;};"
    "x.open('POST','/upload',true);x.send(fd);}"
    "next();}"
    "</script></body></html>");
  server.sendContent(tail);
  server.sendContent("");
}

void handleRoot() {
  Session* s = requireLogin();
  if (!s) return;
  sendFilesPage(s);
}

// ================== VIEW / DOWNLOAD / DELETE / UPLOAD ==================
void handleView() {
  Session* s = requireLogin();
  if (!s) return;
  String path, owner, name;
  if (!buildPath(s, path, owner, name)) return server.send(400, "text/plain", "Bad request");
  if (!SD.exists(path)) return server.send(404, "text/plain", "File not found");

  String lower = name;
  lower.toLowerCase();
  String ct = "";
  if (lower.endsWith(".png")) ct = "image/png";
  else if (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) ct = "image/jpeg";
  else if (lower.endsWith(".gif")) ct = "image/gif";
  else if (lower.endsWith(".pdf")) ct = "application/pdf";
  else if (lower.endsWith(".mp3")) ct = "audio/mpeg";
  else if (lower.endsWith(".wav")) ct = "audio/wav";
  else if (lower.endsWith(".mp4")) ct = "video/mp4";
  else if (lower.endsWith(".txt") || lower.endsWith(".csv") || lower.endsWith(".log")) ct = "text/plain; charset=utf-8";

  File file = SD.open(path, FILE_READ);
  if (!file) return server.send(500, "text/plain", "Open failed");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  if (ct == "") {  // unknown types are downloaded, never rendered (safer)
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    ct = "application/octet-stream";
  }
  server.streamFile(file, ct);
  file.close();
}

void handleDownload() {
  Session* s = requireLogin();
  if (!s) return;
  String path, owner, name;
  if (!buildPath(s, path, owner, name)) return server.send(400, "text/plain", "Bad request");
  if (!SD.exists(path)) return server.send(404, "text/plain", "File not found");

  File file = SD.open(path, FILE_READ);
  if (!file) return server.send(500, "text/plain", "Open failed");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.streamFile(file, "application/octet-stream");
  file.close();
}

void handleDelete() {
  Session* s = requireLogin();
  if (!s) return;
  if (!s->admin) return server.send(403, "text/plain", "Only the admin can delete files");
  String path, owner, name;
  if (buildPath(s, path, owner, name) && SD.exists(path)) {
    SD.remove(path);
  }
  redirect("/");
}

void handleFileUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadOK = false;
    Session* s = getSession();
    if (!s || !sdOK) return;
    ensureShared();
    String fname = cleanFileName(up.filename);
    String base = fname, ext = "";
    int dot = fname.lastIndexOf('.');
    if (dot > 0) { base = fname.substring(0, dot); ext = fname.substring(dot); }
    String path = String(SHARED_DIR) + "/" + fname;
    // never overwrite someone else's file: add _1, _2 ... if the name already exists
    for (int n = 1; SD.exists(path) && n < 100; n++) path = String(SHARED_DIR) + "/" + base + "_" + String(n) + ext;
    if (SD.exists(path)) return;
    uploadFile = SD.open(path, FILE_WRITE);
    uploadOK = (bool)uploadFile;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadOK) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (uploadOK) uploadFile.close();
    uploadOK = false;
    lastCardCalc = 0;
  }
}

void handleUploadDone() {
  if (!getSession()) return server.send(401, "text/plain", "Login required");
  server.send(200, "text/plain", "OK");
}

// ================== ADMIN PANEL ==================
void handleAdmin() {
  Session* s = requireLogin();
  if (!s) return;
  if (!s->admin) return server.send(403, "text/plain", "Admins only");

  String h = pageHead("Admin Panel");
  h += "<div class='card'><div class='header'><h2>&#128737; Admin Panel</h2><div class='nav'>";
  h += "<a href='/'>Files</a><a href='/logout'>Logout</a></div></div></div>";

  // Pending
  h += "<div class='card'><h3 style='margin-bottom:10px'>Pending requests</h3>";
  String pl = listUsers('P');
  if (pl.length() == 0) h += "<p class='empty'>No pending requests.</p>";
  else {
    h += "<table><tbody>";
    int start = 0;
    while (true) {
      int i = pl.indexOf(',', start);
      if (i < 0) break;
      String n = pl.substring(start, i);
      start = i + 1;
      h += "<tr><td><b>" + htmlEscape(n) + "</b> wants to join</td><td><div class='actions'>";
      h += "<form method='POST' action='/admin/approve'><input type='hidden' name='user' value='" + htmlEscape(n) + "'><button class='btn btn-ok'>Approve</button></form>";
      h += "<form method='POST' action='/admin/delete'><input type='hidden' name='user' value='" + htmlEscape(n) + "'><button class='btn btn-danger'>Reject</button></form>";
      h += "</div></td></tr>";
    }
    h += "</tbody></table>";
  }
  h += "</div>";

  // Active users
  h += "<div class='card'><h3 style='margin-bottom:10px'>Active users</h3>";
  String al = listUsers('A');
  if (al.length() == 0) h += "<p class='empty'>No active users yet.</p>";
  else {
    h += "<table><tbody>";
    int start = 0;
    while (true) {
      int i = al.indexOf(',', start);
      if (i < 0) break;
      String n = al.substring(start, i);
      start = i + 1;
      h += "<tr><td><b>" + htmlEscape(n) + "</b></td><td><div class='actions'>";
      h += "<form method='POST' action='/admin/delete' onsubmit=\"return confirm('Delete this user?');\">";
      h += "<input type='hidden' name='user' value='" + htmlEscape(n) + "'><button class='btn btn-danger'>Delete user</button></form>";
      h += "</div></td></tr>";
    }
    h += "</tbody></table>";
  }
  h += "</div></div></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", h);
}

void handleAdminAction(char mode) {
  Session* s = requireLogin();
  if (!s) return;
  if (!s->admin) return server.send(403, "text/plain", "Admins only");
  String u = server.arg("user");
  if (validUsername(u)) {
    if (mode == 'A') {
      updateUser(u, 'A');
      ensureShared();
    } else {
      killSessionsOf(u);
      removeUserFolder(u);
      updateUser(u, 'D');
      lastCardCalc = 0;
    }
  }
  redirect("/admin");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, SPI, 10000000) && !SD.begin(SD_CS, SPI, 4000000)) {
    Serial.println("SD Mount FAILED! Check wiring / card (FAT32).");
  } else {
    sdOK = true;
    Serial.println("SD Mount OK");
    ensureShared();
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 8);   // channel 6, visible, up to 8 devices (default is only 4!)
  WiFi.setSleep(false);                      // faster response
  IPAddress ip = WiFi.softAPIP();
  Serial.print("Hotspot: ");
  Serial.println(AP_SSID);
  Serial.print("Open in browser: http://");
  Serial.println(ip);

  dnsServer.start(DNS_PORT, "*", ip);

  const char* hdrs[] = {"Cookie"};
  server.collectHeaders(hdrs, 1);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLoginGet);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/register", HTTP_GET, handleRegisterGet);
  server.on("/register", HTTP_POST, handleRegisterPost);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/view", HTTP_GET, handleView);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/upload", HTTP_POST, handleUploadDone, handleFileUpload);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin/approve", HTTP_POST, []() { handleAdminAction('A'); });
  server.on("/admin/delete", HTTP_POST, []() { handleAdminAction('D'); });

  // Connectivity-check URLs: answer them like "internet is fine" so Android/iPhone/Windows
  // stay on this Wi-Fi (no "no internet" disconnects, no broken pop-up mini-browser).
  server.on("/generate_204", HTTP_ANY, []() { server.send(204, "text/plain", ""); });
  server.on("/gen_204", HTTP_ANY, []() { server.send(204, "text/plain", ""); });
  server.on("/favicon.ico", HTTP_ANY, []() { server.send(204, "text/plain", ""); });
  server.on("/hotspot-detect.html", HTTP_ANY, []() { server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
  server.on("/library/test/success.html", HTTP_ANY, []() { server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
  server.on("/connecttest.txt", HTTP_ANY, []() { server.send(200, "text/plain", "Microsoft Connect Test"); });
  server.on("/ncsi.txt", HTTP_ANY, []() { server.send(200, "text/plain", "Microsoft NCSI"); });
  server.on("/success.txt", HTTP_ANY, []() { server.send(200, "text/plain", "success\n"); });

  // Anything else -> always go to the ESP32 IP (keeps one cookie / one login per device)
  server.onNotFound([]() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
  Serial.println("Server started.");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
