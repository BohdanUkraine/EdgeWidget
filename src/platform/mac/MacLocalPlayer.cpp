#include "platform/LocalPlayer.h"

#include "platform/CoverFetcher.h"

#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <stdexcept>

namespace {
constexpr int kScriptTimeoutMs = 3000;

const QString kTell = QStringLiteral("tell application id \"com.spotify.client\"");

const QString kStateScript = QStringLiteral(
    "if application id \"com.spotify.client\" is running then\n"
    "  %1\n"
    "    set out to (player state as string)\n"
    "    try\n"
    "      set t to current track\n"
    "      set out to out & linefeed & (name of t) & linefeed & (artist of t) & linefeed & (album of t)"
    " & linefeed & (duration of t) & linefeed & ((player position * 1000) as integer)"
    " & linefeed & (shuffling as string) & linefeed & (repeating as string) & linefeed & (artwork url of t)\n"
    "    end try\n"
    "    return out\n"
    "  end tell\n"
    "end if\n"
    "return \"\"").arg(kTell);

QString runScript(const QString &script)
{
    QProcess p;
    p.start(QStringLiteral("/usr/bin/osascript"), { QStringLiteral("-e"), script });
    if (!p.waitForFinished(kScriptTimeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        throw std::runtime_error("osascript timed out");
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        throw std::runtime_error("osascript failed");
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void tell(const QString &command)
{
    runScript(kTell + QStringLiteral(" to ") + command);
}

class MacLocalPlayer final : public LocalPlayer
{
public:
    MacLocalPlayer() = default;

    void reset() override {}

    MediaState poll() override
    {
        MediaState st;
        const QStringList lines = runScript(kStateScript).split(QLatin1Char('\n'));
        if (lines.size() < 9 || lines[0] == QLatin1String("stopped"))
            return st;
        st.available = true;
        st.playing = lines[0] == QLatin1String("playing");
        st.title = lines[1];
        st.artist = lines[2];
        st.album = lines[3];
        st.durationMs = std::max<qint64>(0, lines[4].toLongLong());
        st.positionMs = std::clamp<qint64>(lines[5].toLongLong(), 0, st.durationMs);
        st.sampledAtMs = MediaState::nowMs();
        st.shuffle = lines[6] == QLatin1String("true");
        st.repeat = lines[7] == QLatin1String("true") ? 2 : 0;
        st.cover = m_covers.get(lines[8]);
        return st;
    }

    void togglePlayPause() override { tell(QStringLiteral("playpause")); }
    void play() override { tell(QStringLiteral("play")); }
    void next() override { tell(QStringLiteral("next track")); }
    void previous() override { tell(QStringLiteral("previous track")); }

    void setShuffle(bool on) override
    {
        tell(QStringLiteral("set shuffling to ") + (on ? "true" : "false"));
    }

    void setRepeat(int mode) override
    {
        tell(QStringLiteral("set repeating to ") + (mode ? "true" : "false"));
    }

    void seek(qint64 positionMs) override
    {
        tell(QStringLiteral("set player position to ") + QString::number(positionMs / 1000.0, 'f', 3));
    }

private:
    CoverFetcher m_covers;
};
}

std::unique_ptr<LocalPlayer> LocalPlayer::create(std::function<void()>)
{
    return std::make_unique<MacLocalPlayer>();
}
