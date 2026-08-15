// Tests fuer die Netzwerkscanner-Engine und den virtuellen Provider (ohne Netz).
#include "tests/harness.hpp"

#include "ncssh/core/netfs.hpp"
#include "ncssh/core/netscan.hpp"
#include "ncssh/core/oui_data.hpp"

#include <QSet>

using namespace ncssh::core;

// --- parseTargets ----------------------------------------------------------

TEST(netscan, parse_cidr)
{
    CHECK_EQ(parseTargets(QStringLiteral("192.168.1.0/30")),
             (QStringList{QStringLiteral("192.168.1.1"), QStringLiteral("192.168.1.2")}));
}

TEST(netscan, parse_range_shorthand)
{
    CHECK_EQ(parseTargets(QStringLiteral("10.0.0.5-7")),
             (QStringList{QStringLiteral("10.0.0.5"), QStringLiteral("10.0.0.6"),
                          QStringLiteral("10.0.0.7")}));
}

TEST(netscan, parse_range_full)
{
    CHECK_EQ(parseTargets(QStringLiteral("10.0.0.5-10.0.0.6")),
             (QStringList{QStringLiteral("10.0.0.5"), QStringLiteral("10.0.0.6")}));
}

TEST(netscan, parse_list_dedupe_and_hostname)
{
    CHECK_EQ(parseTargets(QStringLiteral("1.1.1.1, 1.1.1.1 host.local")),
             (QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("host.local")}));
}

TEST(netscan, parse_limit)
{
    CHECK_EQ(parseTargets(QStringLiteral("10.0.0.0/8"), 5).size(), qsizetype(5));
}

// --- Parser ----------------------------------------------------------------

TEST(netscan, parse_arp_windows)
{
    const QString text = QStringLiteral(
        "Interface: 192.168.1.5 --- 0x4\n"
        "  Internet Address      Physical Address      Type\n"
        "  192.168.1.10          aa-bb-cc-dd-ee-ff     dynamic\n");
    CHECK_EQ(parseArp(text, QStringLiteral("192.168.1.10")),
             QStringLiteral("aa:bb:cc:dd:ee:ff"));
}

TEST(netscan, parse_arp_linux)
{
    const QString text = QStringLiteral("192.168.1.10 ether aa:bb:cc:dd:ee:ff C eth0\n");
    CHECK_EQ(parseArp(text, QStringLiteral("192.168.1.10")),
             QStringLiteral("aa:bb:cc:dd:ee:ff"));
}

TEST(netscan, parse_net_view_de)
{
    const QString text = QStringLiteral(
        "Freigegebene Ressourcen auf \\\\192.168.1.10\n\n"
        "Freigabename  Typ    Verwendet als  Kommentar\n"
        "-------------------------------------------------------------------------------\n"
        "C$            Platte\n"
        "Users         Platte\n"
        "Der Befehl wurde erfolgreich ausgeführt.\n");
    CHECK_EQ(parseNetView(text),
             (QStringList{QStringLiteral("C$"), QStringLiteral("Users")}));
}

TEST(netscan, parse_net_view_en)
{
    const QString text = QStringLiteral(
        "Share name  Type   Used as  Comment\n"
        "-------------------------------------------------------------------------------\n"
        "ADMIN$      Disk\n"
        "Public      Disk\n"
        "The command completed successfully.\n");
    CHECK_EQ(parseNetView(text),
             (QStringList{QStringLiteral("ADMIN$"), QStringLiteral("Public")}));
}

TEST(netscan, parse_smbclient)
{
    const QString text = QStringLiteral("Disk|share1|comment\nDisk|public|\nIPC|IPC$|\n");
    CHECK_EQ(parseSmbclient(text),
             (QStringList{QStringLiteral("share1"), QStringLiteral("public")}));
}

TEST(netscan, presets_have_smb)
{
    CHECK(portPresets().value(QStringLiteral("common")).contains(445));
    CHECK_EQ(portPresets().value(QStringLiteral("smb")), (QVector<int>{139, 445}));
}

TEST(netscan, web_urls)
{
    CHECK_EQ(webUrls(QStringLiteral("1.2.3.4"), {80, 443, 8080, 22}),
             (QStringList{QStringLiteral("http://1.2.3.4"), QStringLiteral("https://1.2.3.4"),
                          QStringLiteral("http://1.2.3.4:8080")}));
    CHECK(webUrls(QStringLiteral("1.2.3.4"), {22, 445}).isEmpty());
}

TEST(netscan, bare_unc_host)
{
    const QString bs = QStringLiteral("\\");
    CHECK_EQ(bareUncHost(bs + bs + QStringLiteral("192.168.0.1")),
             QStringLiteral("192.168.0.1"));
    CHECK_EQ(bareUncHost(bs + bs + QStringLiteral("srv01") + bs), QStringLiteral("srv01"));
    // Vollstaendiger UNC-Pfad mit Freigabe -> kein blosser Host
    CHECK(bareUncHost(bs + bs + QStringLiteral("192.168.0.1") + bs
                      + QStringLiteral("share")).isEmpty());
    CHECK(bareUncHost(QStringLiteral("C:\\temp")).isEmpty());
    CHECK(bareUncHost(QStringLiteral("192.168.0.1")).isEmpty());
}

TEST(netscan, parse_ports)
{
    CHECK_EQ(parsePorts(QStringLiteral("22,80,1000-1003")),
             (QVector<int>{22, 80, 1000, 1001, 1002, 1003}));
    CHECK_EQ(parsePorts(QStringLiteral("443")), (QVector<int>{443}));
    CHECK_EQ(parsePorts(QStringLiteral("bad, 70000, 22")), (QVector<int>{22}));
}

TEST(netscan, service_name)
{
    CHECK_EQ(serviceName(3389), QStringLiteral("RDP"));
    CHECK_EQ(serviceName(22), QStringLiteral("SSH"));
    CHECK_EQ(serviceName(12345), QStringLiteral("12345"));
}

TEST(netscan, parse_ping_and_os)
{
    auto [ttl1, lat1] = parsePing(QStringLiteral("bytes=32 time=3ms TTL=117"));
    CHECK_EQ(ttl1, 117);
    CHECK_EQ(lat1, 3.0);
    auto [ttl2, lat2] = parsePing(QStringLiteral("64 bytes ... ttl=64 time=0,5 ms"));
    CHECK_EQ(ttl2, 64);
    CHECK_EQ(lat2, 0.5);
    CHECK_EQ(osFromTtl(64), QStringLiteral("Linux/Unix"));
    CHECK_EQ(osFromTtl(128), QStringLiteral("Windows"));
    CHECK_EQ(osFromTtl(250), QStringLiteral("Netzwerkgerät"));
    CHECK(osFromTtl(0).isEmpty());
}

TEST(netscan, parse_nbtstat_and_title)
{
    CHECK_EQ(parseNbtstat(QStringLiteral("   PC1   <00>  UNIQUE   Registered")),
             QStringLiteral("PC1"));
    CHECK_EQ(parseHtmlTitle(QStringLiteral("<title> Hello  World </title>")),
             QStringLiteral("Hello World"));
    CHECK(parseHtmlTitle(QStringLiteral("<p>kein titel</p>")).isEmpty());
}

TEST(netscan, host_services)
{
    HostResult hr;
    hr.ip = QStringLiteral("1.2.3.4");
    hr.openPorts = {22, 80, 445};
    CHECK_EQ(hr.services(), (QStringList{QStringLiteral("SSH"), QStringLiteral("HTTP"),
                                         QStringLiteral("SMB")}));
}

TEST(netscan, oui_vendor)
{
    CHECK_EQ(ouiVendor(QStringLiteral("B8:27:EB:00:11:22")), QStringLiteral("Raspberry Pi"));
    CHECK_EQ(ouiVendor(QStringLiteral("08:00:27:00:11:22")),
             QStringLiteral("VirtualBox (Oracle)"));
    CHECK(ouiVendor(QString()).isEmpty());
}

TEST(netscan, wake_on_lan_validation)
{
    CHECK_EQ(wakeOnLan(QStringLiteral("not-a-mac")), false);
    // Gueltige MAC: Rueckgabe haengt vom Netz ab, darf aber nicht werfen.
    (void)wakeOnLan(QStringLiteral("aa:bb:cc:dd:ee:ff"));
}

TEST(netscan, scan_events_empty)
{
    ScanOptions opts;   // keine Ziele
    int hosts = 0;
    scanEvents(opts, nullptr, [&hosts](const HostResult &) { ++hosts; });
    CHECK_EQ(hosts, 0);
}

// --- NetworkScanProvider ---------------------------------------------------

static std::vector<HostResult> sampleHosts()
{
    HostResult pc1;
    pc1.ip = QStringLiteral("192.168.1.10");
    pc1.hostname = QStringLiteral("pc1");
    pc1.mac = QStringLiteral("aa:bb:cc:dd:ee:ff");
    pc1.openPorts = {445, 139, 80};
    pc1.shares = {QStringLiteral("C$"), QStringLiteral("Users")};
    pc1.web = {QStringLiteral("http://192.168.1.10")};
    HostResult other;
    other.ip = QStringLiteral("192.168.1.20");
    other.openPorts = {80};
    return {pc1, other};
}

TEST(netfs, provider_host_entries_carry_metadata)
{
    NetworkScanProvider p(sampleHosts());
    const auto entries = p.listDir(QStringLiteral("net://"));
    QSet<QString> names;
    for (const auto &e : entries)
        names.insert(e.name);
    CHECK_EQ(names, (QSet<QString>{QStringLiteral("pc1"), QStringLiteral("192.168.1.20")}));

    for (const auto &e : entries) {
        if (e.name != QLatin1String("pc1"))
            continue;
        CHECK_EQ(e.extra.value(QStringLiteral("ip")).toString(),
                 QStringLiteral("192.168.1.10"));
        CHECK_EQ(e.extra.value(QStringLiteral("mac")).toString(),
                 QStringLiteral("aa:bb:cc:dd:ee:ff"));
        CHECK_EQ(e.extra.value(QStringLiteral("shares")).toBool(), true);
        CHECK_EQ(e.extra.value(QStringLiteral("web")).toStringList(),
                 (QStringList{QStringLiteral("http://192.168.1.10")}));
        CHECK(e.isDir());
    }
}

TEST(netfs, provider_is_host_list)
{
    NetworkScanProvider p(sampleHosts());
    CHECK(p.isHostList(QStringLiteral("net://")));
    CHECK(!p.isHostList(QStringLiteral("net://pc1")));
    CHECK(!p.isHostList(QStringLiteral("net://pc1/C$")));
}

TEST(netfs, provider_path_ops)
{
    NetworkScanProvider p(sampleHosts());
    CHECK_EQ(p.join(QStringLiteral("net://"), QStringLiteral("pc1")),
             QStringLiteral("net://pc1"));
    CHECK_EQ(p.join(QStringLiteral("net://pc1"), QStringLiteral("C$")),
             QStringLiteral("net://pc1/C$"));
    CHECK_EQ(p.parent(QStringLiteral("net://pc1/C$")), QStringLiteral("net://pc1"));
    CHECK_EQ(p.parent(QStringLiteral("net://pc1")), QStringLiteral("net://"));
    CHECK_EQ(p.parent(QStringLiteral("net://")), QStringLiteral("net://"));
    CHECK_EQ(p.basename(QStringLiteral("net://pc1/C$")), QStringLiteral("C$"));
}

TEST(netfs, provider_shares_listing)
{
    NetworkScanProvider p(sampleHosts());
    const auto shares = p.listDir(QStringLiteral("net://pc1"));
    QStringList names;
    for (const auto &s : shares) {
        names << s.name;
        CHECK(s.isDir());
    }
    CHECK_EQ(names, (QStringList{QStringLiteral("C$"), QStringLiteral("Users")}));
}
