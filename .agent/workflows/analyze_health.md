---
description: Analyze project file sizes and identify refactoring candidates.
---

1.  **Scan Codebase:**
    *   List all source files of the project's main language(s) (e.g. `.cs`, `.xaml`, `.ts`, `.tsx`, `.py`).
    *   Sort by line count (descending).

2.  **Analyze Thresholds:**
    *   **Warning:** > 500 lines.
    *   **Critical:** > 800 lines.

3.  **Report:**
    *   Generate a summary table of the top 10 largest files.
    *   Highlight any files exceeding thresholds.
    *   Suggest specific files for refactoring (splitting).

4.  **Persist (Optional):**
    *   If significant changes, update `Antigravity_FileSize_Analysis.txt` (if it exists) or suggest creating it.
