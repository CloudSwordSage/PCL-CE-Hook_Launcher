using System.Globalization;
using System.Text.Json;
using PCL.Core.App.Region;

internal static class Program
{
    // stdout protocol:
    //   Ban       exit 1: Restricted (Mainland or Unknown)
    //   NotBanned exit 0: Unrestricted (Other)
    //   Ban       exit 2: Probe error (not a policy assessment)
    private static int Main(string[] args)
    {
        var debug = false;
        var delayMs = 0;

        for (var i = 0; i < args.Length; i++)
        {
            if (args[i] == "--debug")
            {
                debug = true;
                continue;
            }

            if (args[i] == "--delay-ms" && i + 1 < args.Length &&
                int.TryParse(args[++i], NumberStyles.None, CultureInfo.InvariantCulture, out var ms) &&
                ms is >= 0 and <= 60000)
            {
                delayMs = ms;
                continue;
            }

            return Error("Usage: PclCeRegionProbe [--debug] [--delay-ms 0..60000]");
        }

        if (!OperatingSystem.IsWindows())
            return Error("This probe requires Windows.");

        try
        {
            // No region API is explicitly called before this pause.
            // For native API hooks, prefer launching suspended and injecting before resume.
            if (delayMs > 0)
                Thread.Sleep(delayMs);

            var signals = RegionPolicy.ReadWindowsSignals();
            var assessment = RegionPolicy.Evaluate(signals);

            if (debug)
            {
                var diagnostic = new
                {
                    assessment.Classification,
                    assessment.Reason,
                    assessment.IsRestricted,
                    signals.GeoName,
                    signals.GeoNameAvailable,
                    signals.DeviceRegionGeoId,
                    signals.DeviceRegionAvailable,
                    signals.TimeZoneId,
                    signals.TimeZoneAvailable,
                    signals.UserLocaleName,
                    signals.UserLocaleAvailable,
                    signals.PreferredUiLanguages,
                    signals.PreferredUiLanguagesAvailable
                };
                Console.Error.WriteLine(JsonSerializer.Serialize(diagnostic,
                    new JsonSerializerOptions { WriteIndented = true }));
            }

            Console.Out.WriteLine(assessment.IsRestricted ? "Ban" : "NotBanned");
            return assessment.IsRestricted ? 1 : 0;
        }
        catch (Exception ex)
        {
            return Error($"Probe exception: {ex}");
        }
    }

    private static int Error(string message)
    {
        Console.Error.WriteLine(message);
        Console.Out.WriteLine("Ban");
        return 2;
    }
}
