namespace Dalamud.Common;

/// <summary>Enum describing what to do on unhandled exceptions.</summary>
public enum UnhandledExceptionHandlingMode
{
    /// <summary>Controls how unhandled exceptions are handled during startup and runtime.</summary>
    /// <remarks>See `vectored_exception_handler` in `veh.cpp`.</remarks>
    Default,

    /// <summary>Waits for debugger if none is attached, and pass the exception to the next handler.</summary>
    /// <remarks>See `exception_handler` in `veh.cpp`.</remarks>
    StallDebug,

    /// <summary>Do not register an exception handler.</summary>
    None,
}
