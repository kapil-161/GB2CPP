#include "McpServer.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QProcessEnvironment>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QByteArray>
#include <QStringList>
#include <QSet>

#include "Config.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// stdio transport — line-delimited JSON, using the raw inherited pipe handles.
// (GB2 is a GUI-subsystem app; we must NOT touch the console here or we would
//  disconnect the MCP client's stdin/stdout pipes.)
// ---------------------------------------------------------------------------
namespace {

void writeStdout(const QByteArray &bytes)
{
#ifdef Q_OS_WIN
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    const char *p = bytes.constData();
    DWORD remaining = static_cast<DWORD>(bytes.size());
    while (remaining > 0 && WriteFile(h, p, remaining, &written, nullptr) && written > 0) {
        p += written;
        remaining -= written;
    }
    FlushFileBuffers(h);
#else
    const char *p = bytes.constData();
    ssize_t remaining = bytes.size();
    while (remaining > 0) {
        ssize_t n = ::write(1, p, static_cast<size_t>(remaining));
        if (n <= 0) break;
        p += n;
        remaining -= n;
    }
#endif
}

// Blocking read of a single '\n'-terminated line. Returns false on EOF/error.
bool readStdinLine(QByteArray &buffer, QByteArray &line)
{
    for (;;) {
        int nl = buffer.indexOf('\n');
        if (nl >= 0) {
            line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            if (line.endsWith('\r')) line.chop(1);
            return true;
        }
        char chunk[4096];
#ifdef Q_OS_WIN
        HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
        DWORD got = 0;
        BOOL ok = ReadFile(h, chunk, sizeof(chunk), &got, nullptr);
        if (!ok || got == 0) {
            if (!buffer.isEmpty()) { line = buffer; buffer.clear(); return true; }
            return false; // EOF / pipe closed
        }
        buffer.append(chunk, static_cast<int>(got));
#else
        ssize_t got = ::read(0, chunk, sizeof(chunk));
        if (got <= 0) {
            if (!buffer.isEmpty()) { line = buffer; buffer.clear(); return true; }
            return false;
        }
        buffer.append(chunk, static_cast<int>(got));
#endif
    }
}

void sendMessage(const QJsonObject &msg)
{
    QByteArray out = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    out.append('\n');
    writeStdout(out);
}

void sendResult(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["result"] = result;
    sendMessage(msg);
}

void sendError(const QJsonValue &id, int code, const QString &message)
{
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["error"] = err;
    sendMessage(msg);
}

// MCP tool result helpers -----------------------------------------------------
QJsonObject textContent(const QString &text)
{
    QJsonObject c;
    c["type"] = "text";
    c["text"] = text;
    QJsonArray arr;
    arr.append(c);
    QJsonObject result;
    result["content"] = arr;
    return result;
}

QJsonObject errorToolResult(const QString &text)
{
    QJsonObject result = textContent(text);
    result["isError"] = true;
    return result;
}

// Extensions GB2 treats as DSSAT output files (mirrors DataProcessor).
const QSet<QString> &outputExtensions()
{
    static const QSet<QString> exts = {
        "OUT", "OSU", "CSV", "OVT", "OPT", "OPG", "OEB",
        "OEV", "OG2", "OGF", "OLN", "OLC", "OME"
    };
    return exts;
}

// Run this same executable in headless mode and report whether the output landed.
QJsonObject runHeadless(const QStringList &args, const QString &savePath,
                        const QString &metricsPath, int timeoutMs)
{
    QProcess proc;
    proc.setProgram(QCoreApplication::applicationFilePath());
    proc.setArguments(args);

    // Inherit the environment so the child resolves its Qt plugins the same way
    // the GUI does. Prefer the offscreen platform when its plugin is bundled
    // next to the exe (no flashing window); otherwise let the child pick its
    // default platform.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString exeDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
#ifdef Q_OS_WIN
    if (QFileInfo::exists(exeDir + "/platforms/qoffscreen.dll"))
        env.insert("QT_QPA_PLATFORM", "offscreen");
#else
    if (QFileInfo::exists(exeDir + "/platforms/libqoffscreen.dylib") ||
        !qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM_PLUGIN_PATH"))
        env.insert("QT_QPA_PLATFORM", "offscreen");
#endif
    proc.setProcessEnvironment(env);

    proc.start();
    QJsonObject info;
    if (!proc.waitForStarted(10000)) {
        return errorToolResult("Failed to start GB2 headless process.");
    }
    bool finished = proc.waitForFinished(timeoutMs);
    if (!finished) {
        proc.kill();
        proc.waitForFinished(2000);
        return errorToolResult(QString("GB2 headless render timed out after %1 ms.").arg(timeoutMs));
    }

    bool plotOk = !savePath.isEmpty() && QFileInfo::exists(savePath);
    bool metricsOk = metricsPath.isEmpty() ? true : QFileInfo::exists(metricsPath);

    QJsonObject payload;
    payload["success"] = plotOk;
    payload["plot_path"] = plotOk ? savePath : QString();
    if (!metricsPath.isEmpty())
        payload["metrics_path"] = metricsOk && QFileInfo::exists(metricsPath) ? metricsPath : QString();
    payload["exit_code"] = proc.exitCode();

    QString summary = plotOk
        ? QString("Plot saved to %1").arg(savePath)
        : QString("GB2 finished (exit %1) but no image was produced at %2")
              .arg(proc.exitCode()).arg(savePath);
    if (plotOk && !metricsPath.isEmpty())
        summary += metricsOk ? QString("; metrics saved to %1").arg(metricsPath)
                             : "; metrics file was not produced";

    QJsonObject result = plotOk ? textContent(summary + "\n" +
                                     QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)))
                                : errorToolResult(summary);
    return result;
}

// ---- Tool implementations ---------------------------------------------------

QJsonObject toolListOutputFiles(const QJsonObject &args)
{
    QString cropDir = args.value("crop_dir").toString();
    QDir dir(cropDir);
    if (cropDir.isEmpty() || !dir.exists())
        return errorToolResult(QString("crop_dir does not exist or is not a directory: %1").arg(cropDir));

    QStringList files;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        if (outputExtensions().contains(fi.suffix().toUpper()))
            files << fi.fileName();
    }
    QJsonArray arr;
    for (const QString &f : files) arr.append(f);
    return textContent(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QJsonObject toolPlotTimeseries(const QJsonObject &args)
{
    QString cropDir = args.value("crop_dir").toString();
    QString xvar = args.value("xvar").toString();
    QString savePath = args.value("save_path").toString();
    if (cropDir.isEmpty() || xvar.isEmpty() || savePath.isEmpty())
        return errorToolResult("plot_timeseries requires crop_dir, xvar and save_path.");

    QStringList outputFiles;
    for (const QJsonValue &v : args.value("output_files").toArray()) outputFiles << v.toString();
    if (outputFiles.isEmpty())
        return errorToolResult("plot_timeseries requires at least one output_files entry.");

    QStringList yvars;
    for (const QJsonValue &v : args.value("yvars").toArray()) yvars << v.toString();
    if (yvars.isEmpty())
        return errorToolResult("plot_timeseries requires at least one yvars entry.");

    QString dssatBase = args.value("dssat_base").toString();
    if (dssatBase.isEmpty())
#ifdef Q_OS_WIN
        dssatBase = "C:/DSSAT48";
#else
        dssatBase = "/Applications/DSSAT48";
#endif
    QString metricsPath = args.value("metrics_path").toString();
    bool boxplot = args.value("boxplot").toBool(false);
    bool grid = args.value("grid").toBool(false);
    int timeoutMs = static_cast<int>(args.value("timeout_seconds").toDouble(30.0) * 1000.0);

    QDir().mkpath(QFileInfo(savePath).absolutePath());
    if (!metricsPath.isEmpty()) QDir().mkpath(QFileInfo(metricsPath).absolutePath());

    QStringList a;
    a << dssatBase << cropDir;
    a << outputFiles;
    a << "--xvar" << xvar << "--yvar" << yvars.join(",") << "--save" << savePath;
    if (!metricsPath.isEmpty()) a << "--metrics" << metricsPath;
    if (boxplot) a << "--boxplot";
    if (grid) a << "--grid";

    return runHeadless(a, savePath, metricsPath, timeoutMs);
}

QJsonObject toolPlotScatter(const QJsonObject &args)
{
    QString cropName = args.value("crop_name").toString();
    QString savePath = args.value("save_path").toString();
    if (cropName.isEmpty() || savePath.isEmpty())
        return errorToolResult("plot_scatter requires crop_name and save_path.");

    QStringList scatterVars, scatterMetrics;
    for (const QJsonValue &v : args.value("scatter_vars").toArray()) scatterVars << v.toString();
    for (const QJsonValue &v : args.value("scatter_metrics").toArray()) scatterMetrics << v.toString();
    int timeoutMs = static_cast<int>(args.value("timeout_seconds").toDouble(30.0) * 1000.0);

    QDir().mkpath(QFileInfo(savePath).absolutePath());

    QStringList a;
    a << cropName << "--scatter";
    if (!scatterVars.isEmpty()) a << "--scatter-vars" << scatterVars.join(",");
    if (!scatterMetrics.isEmpty()) a << "--scatter-metrics" << scatterMetrics.join(",");
    a << "--save" << savePath;

    return runHeadless(a, savePath, QString(), timeoutMs);
}

QJsonObject toolReadPlotImage(const QJsonObject &args)
{
    QString path = args.value("path").toString();
    QFile f(path);
    if (path.isEmpty() || !f.exists())
        return errorToolResult(QString("No such file: %1").arg(path));
    if (!f.open(QIODevice::ReadOnly))
        return errorToolResult(QString("Cannot read file: %1").arg(path));
    QByteArray data = f.readAll();
    f.close();

    QString mime = path.endsWith(".jpg", Qt::CaseInsensitive) || path.endsWith(".jpeg", Qt::CaseInsensitive)
                       ? "image/jpeg" : "image/png";
    QJsonObject img;
    img["type"] = "image";
    img["data"] = QString::fromUtf8(data.toBase64());
    img["mimeType"] = mime;
    QJsonArray arr;
    arr.append(img);
    QJsonObject result;
    result["content"] = arr;
    return result;
}

// ---- tools/list schema ------------------------------------------------------

QJsonObject strSchema(const QString &desc)
{
    QJsonObject o; o["type"] = "string"; o["description"] = desc; return o;
}
QJsonObject arrStrSchema(const QString &desc)
{
    QJsonObject items; items["type"] = "string";
    QJsonObject o; o["type"] = "array"; o["items"] = items; o["description"] = desc; return o;
}

QJsonArray toolDefinitions()
{
    QJsonArray tools;

    { // list_output_files
        QJsonObject props;
        props["crop_dir"] = strSchema("Absolute path to a crop's output directory (e.g. C:/DSSAT48/Wheat).");
        QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
        QJsonArray req; req.append("crop_dir"); schema["required"] = req;
        QJsonObject t; t["name"] = "list_output_files";
        t["description"] = "List DSSAT output files (PlantGro.OUT, Evaluate.OUT, ...) in a crop directory.";
        t["inputSchema"] = schema;
        tools.append(t);
    }
    { // plot_timeseries
        QJsonObject props;
        props["crop_dir"] = strSchema("Absolute path to the crop's output directory.");
        props["output_files"] = arrStrSchema("DSSAT output filenames within crop_dir, e.g. [\"PlantGro.OUT\"].");
        props["xvar"] = strSchema("X-axis variable code, e.g. DAS or DATE.");
        props["yvars"] = arrStrSchema("Y-axis variable code(s), e.g. [\"LAID\",\"TOPWT\"].");
        props["save_path"] = strSchema("Absolute path to write the plot image (.png or .pdf).");
        props["dssat_base"] = strSchema("DSSAT install base dir. Defaults per-OS if omitted.");
        props["metrics_path"] = strSchema("Optional absolute path to also save fit metrics as CSV.");
        QJsonObject boxp; boxp["type"] = "boolean"; boxp["description"] = "Render as a box plot instead of lines.";
        props["boxplot"] = boxp;
        QJsonObject gridp; gridp["type"] = "boolean";
        gridp["description"] = "Tile an experiment x variable grid (rows=experiment, cols=variable) when 2+ experiments are present.";
        props["grid"] = gridp;
        QJsonObject tos; tos["type"] = "number"; tos["description"] = "Max seconds to wait for the render.";
        props["timeout_seconds"] = tos;
        QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
        QJsonArray req; req.append("crop_dir"); req.append("output_files"); req.append("xvar"); req.append("yvars"); req.append("save_path");
        schema["required"] = req;
        QJsonObject t; t["name"] = "plot_timeseries";
        t["description"] = "Generate a headless time-series (or box) plot from DSSAT output and save it as PNG/PDF.";
        t["inputSchema"] = schema;
        tools.append(t);
    }
    { // plot_scatter
        QJsonObject props;
        props["crop_name"] = strSchema("Crop name as GB2 knows it (e.g. Wheat), not a path.");
        props["save_path"] = strSchema("Absolute path to write the plot image (.png).");
        props["scatter_vars"] = arrStrSchema("Variable codes to plot (e.g. [\"ADAP\",\"CWAM\"]). Auto if omitted.");
        props["scatter_metrics"] = arrStrSchema("Fit statistics to annotate, e.g. [\"RMSE\",\"R2\",\"d-stat\"].");
        QJsonObject tos; tos["type"] = "number"; tos["description"] = "Max seconds to wait for the render.";
        props["timeout_seconds"] = tos;
        QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
        QJsonArray req; req.append("crop_name"); req.append("save_path"); schema["required"] = req;
        QJsonObject t; t["name"] = "plot_scatter";
        t["description"] = "Generate a headless simulated-vs-measured scatter plot from a crop's Evaluate.OUT.";
        t["inputSchema"] = schema;
        tools.append(t);
    }
    { // read_plot_image
        QJsonObject props;
        props["path"] = strSchema("Absolute path to a .png produced by plot_timeseries or plot_scatter.");
        QJsonObject schema; schema["type"] = "object"; schema["properties"] = props;
        QJsonArray req; req.append("path"); schema["required"] = req;
        QJsonObject t; t["name"] = "read_plot_image";
        t["description"] = "Return a previously generated plot image so it can be viewed inline.";
        t["inputSchema"] = schema;
        tools.append(t);
    }
    return tools;
}

QJsonObject dispatchToolCall(const QString &name, const QJsonObject &args)
{
    if (name == "list_output_files") return toolListOutputFiles(args);
    if (name == "plot_timeseries")   return toolPlotTimeseries(args);
    if (name == "plot_scatter")      return toolPlotScatter(args);
    if (name == "read_plot_image")   return toolReadPlotImage(args);
    return errorToolResult(QString("Unknown tool: %1").arg(name));
}

} // namespace

// ---------------------------------------------------------------------------
int runMcpServer(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QByteArray buffer;
    QByteArray lineBytes;
    while (readStdinLine(buffer, lineBytes)) {
        if (lineBytes.trimmed().isEmpty()) continue;

        QJsonParseError perr;
        QJsonDocument doc = QJsonDocument::fromJson(lineBytes, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            sendError(QJsonValue(), -32700, "Parse error");
            continue;
        }
        QJsonObject req = doc.object();
        QString method = req.value("method").toString();
        bool isRequest = req.contains("id");
        QJsonValue id = req.value("id");

        if (method == "initialize") {
            QJsonObject caps;
            caps["tools"] = QJsonObject();
            QJsonObject serverInfo;
            serverInfo["name"] = "gb2";
            serverInfo["version"] = Config::APP_VERSION;
            QJsonObject result;
            // Echo the client's protocol version when given, else a known-good default.
            QString proto = req.value("params").toObject().value("protocolVersion").toString();
            result["protocolVersion"] = proto.isEmpty() ? QString("2024-11-05") : proto;
            result["capabilities"] = caps;
            result["serverInfo"] = serverInfo;
            sendResult(id, result);
        }
        else if (method == "notifications/initialized" || method == "initialized") {
            // notification — no response
        }
        else if (method == "ping") {
            if (isRequest) sendResult(id, QJsonObject());
        }
        else if (method == "tools/list") {
            QJsonObject result;
            result["tools"] = toolDefinitions();
            sendResult(id, result);
        }
        else if (method == "tools/call") {
            QJsonObject params = req.value("params").toObject();
            QString name = params.value("name").toString();
            QJsonObject arguments = params.value("arguments").toObject();
            QJsonObject result = dispatchToolCall(name, arguments);
            sendResult(id, result);
        }
        else {
            if (isRequest) sendError(id, -32601, QString("Method not found: %1").arg(method));
        }
    }
    return 0;
}
