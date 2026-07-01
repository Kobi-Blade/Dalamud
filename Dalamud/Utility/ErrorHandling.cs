using System.Runtime.InteropServices;

using Serilog;

namespace Dalamud.Utility;

/// <summary>
/// Utilities for handling errors inside Dalamud.
/// </summary>
internal static partial class ErrorHandling
{
    /// <summary>
    /// Crash the game at this point without opening a crash handler.
    /// </summary>
    /// <param name="context">The context to log before exiting.</param>
    public static void CrashWithContext(string context)
    {
        Log.Fatal("CrashWithContext requested: {Context}", context);
        Environment.Exit(-1);
    }

    /// <summary>
    /// Check the stack trace for indications of a smart app control error, and show a message box and terminate the process
    /// if found.
    /// </summary>
    /// <param name="exception">The exception to search in.</param>
    public static void ShowSystemIntegrityPolicyErrorIfApplicable(Exception? exception)
    {
        if (exception == null)
        {
            return;
        }

        // ERROR_SYSTEM_INTEGRITY_POLICY_VIOLATION
        const string indicator = "0x800711C7";

        if (exception.ToString().Contains(indicator))
        {
            Util.Fatal("Windows Smart App Control has blocked Dalamud from loading, and the game cannot continue.\n" +
                       "You must disable Smart App Control or Dalamud to start the game.\n\n" +
                       "Press OK to open a guide in your web browser.", "Dalamud Error", false);

            Util.OpenLink("https://goatcorp.github.io/faq/xl_troubleshooting#q-how-do-i-disable-smart-app-control");
            Log.CloseAndFlush();
            Environment.Exit(-1);
        }
    }
}
