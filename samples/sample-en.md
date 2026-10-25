---
title: qMDView Feature Tour
author: SAD
---

# qMDView Feature Tour

This document shows what **qMDView** can display. The Contents panel on the left builds an outline from the headings — click an entry to jump to it. The current section is highlighted as you scroll.

## Text Formatting

Paragraphs can mix **bold**, *italic*, ~~strikethrough~~ and `inline code`. Long lines wrap normally, and the line height is set to 150% for comfortable reading.

> This is a block quote.
> A [link](https://www.qt.io) inside a quote keeps its link color.

## Lists

- First level
  - Second level
    - Third level
- Another item

1. Numbered list
2. Second point
3. Third point

### Task List

- [x] Read Markdown files
- [x] Reload automatically when the file changes
- [ ] Export to PDF

## Code

```cpp
// Fenced code blocks get a background and a border
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
```

Indented code block:

    uint16_t crc = 0xFFFF;
    crc ^= data;

## Tables

| Item | Spec | Notes |
|:-----|:----:|------:|
| Parser | md4c | Built into Qt |
| Dialect | GitHub | Tables, task lists |
| Encoding | UTF-8 / UTF-16 | Detected automatically |

## Images

Relative image paths are resolved from the folder of the Markdown file:

![Sample chart](images/chart.png)

## Links

- Web address: <https://doc.qt.io/qt-6/qtextdocument.html>
- Anchors in this document: [jump to "Tables"](#tables), [jump to "Code"](#code)
- Other Markdown files: [second.md](second.md)

---

## The End

Edit and save this file in any editor — qMDView reloads it automatically.
