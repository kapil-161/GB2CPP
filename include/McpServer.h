#ifndef MCPSERVER_H
#define MCPSERVER_H

// Built-in MCP (Model Context Protocol) server so GB2.exe is directly
// agent-connectable with no separate adapter:  GB2.exe --mcp
//
// Speaks line-delimited JSON-RPC 2.0 over stdin/stdout (the MCP stdio
// transport). Plot tools are executed by launching this same executable in
// headless mode (--save / --scatter), reusing the proven CLI rendering path.
//
// Returns a process exit code; call this INSTEAD of starting the GUI when the
// command line contains --mcp. Must run before any console/GUI setup so the
// inherited stdio pipes from the MCP client stay intact.
int runMcpServer(int argc, char **argv);

#endif // MCPSERVER_H
