#!/bin/sh
# Nova Programming Language — Install Script (macOS / Linux)
# Usage: curl -fsSL <raw-url>/install.sh | sh
set -e

REPO_URL="https://github.com/Cosmic4796/Nova.git"
INSTALL_DIR="/usr/local/bin"
TMP_DIR="$(mktemp -d)"

echo "==> Installing Nova Programming Language..."

# Check dependencies
for cmd in git cmake cc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "Error: '$cmd' is required but not found."
        echo "Please install it and try again."
        exit 1
    fi
done

# Clone repository
echo "==> Cloning repository..."
git clone --quiet --depth 1 "$REPO_URL" "$TMP_DIR/nova" 2>/dev/null || {
    echo "Error: Failed to clone repository."
    echo "You can build from source manually:"
    echo "  git clone $REPO_URL && cd nova && cmake -B build && cmake --build build"
    rm -rf "$TMP_DIR"
    exit 1
}

# Build
echo "==> Building Nova..."
cd "$TMP_DIR/nova"
cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
cmake --build build >/dev/null 2>&1

# Install binary
echo "==> Installing to $INSTALL_DIR..."
if [ -w "$INSTALL_DIR" ]; then
    cp build/nova "$INSTALL_DIR/nova"
    mkdir -p "$INSTALL_DIR/stdlib"
    cp build/stdlib/* "$INSTALL_DIR/stdlib/"
else
    echo "==> Need sudo to install to $INSTALL_DIR"
    sudo cp build/nova "$INSTALL_DIR/nova"
    sudo mkdir -p "$INSTALL_DIR/stdlib"
    sudo cp build/stdlib/* "$INSTALL_DIR/stdlib/"
fi

# Cleanup
rm -rf "$TMP_DIR"

# Verify
if command -v nova >/dev/null 2>&1; then
    echo ""
    echo "==> Nova installed successfully!"
    nova version
    echo ""
    echo "Get started:"
    echo "  nova              # Start the REPL"
    echo "  nova init myapp   # Create a new project"
    echo "  nova help         # Show all commands"
else
    echo ""
    echo "==> Nova binary installed to $INSTALL_DIR/nova"
    echo "Make sure $INSTALL_DIR is in your PATH."
fi
