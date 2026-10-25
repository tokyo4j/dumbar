## Policy
- We are writing a new wayland panel, Dumbar.
- It's a minimalistic, standalone, stacking wayland panel.
- We focus on simplicity
  - We always try not to introduce abstractions which often makes the code harder to read
  - We intentionally use assert() in many places for simplification

## Rules
- All code must be self-explanatory and cleanly structured.
- For every task, consider how it will impact the architecture and code quality, not just the immediate problem.
  - I don't mind if you have to make massive refactors to improve the architecture and code quality.
  - Prefer fixes that also simplify, e.g. by removing the special case or state that caused the bug, over fixes that add code on top.
- Do not use *defensive* design unless it fixes real issues. It often unnecessarily complicates the code.
- Do not care about compatibility of APIs, protocols, configuration, etc.
- Follow the existing code's pattern but offer refactors if they improve code quality and maintainability.
- Generally avoid using non-const static/global variables. If you need them, request user's permission.
