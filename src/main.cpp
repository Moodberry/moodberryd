// moodberryd.cpp - Init and service manager daemon (PID 1)

#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <ctime>
#include <yaml-cpp/yaml.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sstream>

namespace fs = std::filesystem;

struct Service {
    std::string id;
    std::string name;
    std::string desc;
    std::string start;
    std::string user;
    std::string group;
    std::vector<std::string> needs;
    bool enabled = true;
    pid_t pid = -1;
};

std::map<std::string, Service> services;
std::set<std::string> started;

std::string mb_name = "OpenMB";
std::string mb_icon = "🍇";
std::string mb_color = "\033[35m";
const std::string reset_color = "\033[0m";

const std::string log_path = "/var/log/openmb.log";
std::ofstream logOut(log_path, std::ios::app);

void log(const std::string& msg) {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "<%H:%M:%S, %b %d> ", tm);
    std::string entry = buf + msg;
    std::cout << entry << std::endl;
    logOut << entry << std::endl;
}

void loadPrefs() {
    try {
        YAML::Node prefs = YAML::LoadFile("/etc/moodberry/prefs.yml");
        if (prefs["name"]) mb_name = prefs["name"].as<std::string>();
        if (prefs["icon"]) mb_icon = prefs["icon"].as<std::string>();
        if (prefs["color"]) mb_color = "\033[35m"; // simplified
    } catch (...) {}
}

Service parseService(const fs::path& path) {
    YAML::Node node = YAML::LoadFile(path.string());
    Service s;
    s.id = path.filename().string();
    s.name = node["name"] ? node["name"].as<std::string>() : path.stem().string();
    s.desc = node["description"] ? node["description"].as<std::string>() : "";
    s.start = node["start"] ? node["start"].as<std::string>() : "";
    s.user = node["user"] ? node["user"].as<std::string>() : "root";
    s.group = node["group"] ? node["group"].as<std::string>() : "root";
    s.enabled = node["enabled"] ? node["enabled"].as<bool>() : true;
    if (node["needs"])
        for (auto& n : node["needs"])
            s.needs.push_back(n.as<std::string>());
    return s;
}

void writeEnabled(const std::string& id, bool enable) {
    fs::path path = "/etc/moodberry/services/" + id;
    YAML::Node node = YAML::LoadFile(path);
    node["enabled"] = enable;
    std::ofstream fout(path);
    fout << node;
}

void startServiceRecursive(const std::string& id) {
    if (started.count(id)) return;
    if (!services.count(id)) return;

    Service& s = services[id];
    if (!s.enabled && id != "moodberryd.service") return;

    for (const auto& dep : s.needs)
        startServiceRecursive(dep);

    if (id != "moodberryd.service")
        log("< * > Loading " + s.name + (s.desc.empty() ? "" : " - " + s.desc) + "...");

    pid_t pid = fork();
    if (pid == 0) {
        execl(s.start.c_str(), s.start.c_str(), nullptr);
        _exit(1);
    } else if (pid > 0) {
        s.pid = pid;
        started.insert(id);
        if (id != "moodberryd.service")
            log("< ✓ > Loaded " + s.name + (s.desc.empty() ? "" : " - " + s.desc) + ".");
    }
}

std::string getServiceStatus(const std::string& id) {
    if (!services.count(id)) return "not-found";
    if (services[id].pid > 0 && kill(services[id].pid, 0) == 0) return "running";
    if (started.count(id)) return "failed";
    return "stopped";
}

void handleRequest(const std::string& cmd, const std::string& id, int client) {
    if (!services.count(id)) {
        std::string msg = "Service not found\n";
        send(client, msg.c_str(), msg.size(), 0);
        return;
    }

    if (cmd == "enable") {
        writeEnabled(id, true);
        send(client, "Enabled\n", 8, 0);
    } else if (cmd == "disable") {
        writeEnabled(id, false);
        send(client, "Disabled\n", 9, 0);
    } else if (cmd == "start") {
        startServiceRecursive(id);
        send(client, "Started\n", 8, 0);
    } else if (cmd == "stop") {
        kill(services[id].pid, SIGTERM);
        send(client, "Stopped\n", 8, 0);
    } else if (cmd == "status") {
        std::string status = getServiceStatus(id);
        std::string response;
        if (status == "running") response = "🟢 ";
        else if (status == "failed") response = "🔴 ";
        else response = "⚫️ ";
        response += id + ": \"" + services[id].name + "\"\n";

        if (status == "running") {
            response += "    Description: \"" + services[id].desc + "\"\n";
            response += "    Start: " + services[id].start + "\n";
            response += "    Enabled?: " + std::string(services[id].enabled ? "true" : "false") + "\n";
            response += "    Log:\n";
            std::ifstream in(log_path);
            std::string line;
            while (std::getline(in, line)) {
                if (line.find(services[id].name) != std::string::npos) {
                    response += line + "\n";
                }
            }
        }

        send(client, response.c_str(), response.size(), 0);
    }
}

void startSocketServer() {
    const char* sock_path = "/run/moodberry.sock";
    unlink(sock_path);

    int server = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, sock_path);
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 5);

    while (true) {
        int client = accept(server, nullptr, nullptr);
        char buffer[128];
        int len = recv(client, buffer, sizeof(buffer) - 1, 0);
        buffer[len] = 0;
        std::istringstream iss(buffer);
        std::string id, cmd;
        iss >> id >> cmd;
        handleRequest(cmd, id, client);
        close(client);
    }
}

int main() {
    loadPrefs();
    log("");
    log("  " + mb_color + mb_icon + " Welcome to " + mb_name + "!" + reset_color);
    log("");

    fs::path dir = "/etc/moodberry/services";
    if (!fs::exists(dir)) return 1;

    for (const auto& file : fs::directory_iterator(dir)) {
        if (file.path().extension() == ".yml") {
            try {
                Service s = parseService(file.path());
                services[s.id] = s;
            } catch (...) {
                log("  < ! > Error loading: " + file.path().string());
            }
        }
    }

    started.insert("moodberryd.service");
    for (const auto& [id, svc] : services)
        startServiceRecursive(id);

    startSocketServer();
    return 0;
}
