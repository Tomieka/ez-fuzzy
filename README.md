# EZ Fuzzy

A fast and efficient fuzzy file finder application built with Qt.

## Features

- Fast fuzzy file search
- File preview
- Bookmarks for frequently used directories
- File filtering by type
- Dark theme support
- Keyboard shortcuts for quick navigation

## Building from Source

### Prerequisites

- Qt 5.12 or higher
- CMake 3.10 or higher
- C++17 compatible compiler

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/kkomoney/ez-fuzzy.git
cd ez-fuzzy

# Create build directory
mkdir build
cd build

# Configure and build
cmake ..
make

# Run the application
./ez-fuzzy
```

## Usage

1. Click "Browse" to select a directory to search in
2. Type in the search box to find files using fuzzy matching
3. Double-click on a file to open it
4. Use the filter options to narrow down results
5. Add bookmarks for frequently accessed directories

## Keyboard Shortcuts

- Up/Down: Navigate through results
- Enter: Open selected file
- Escape: Clear search or exit
- Ctrl+Q: Exit application

## License

This project is licensed under the MIT License - see the LICENSE file for details. 