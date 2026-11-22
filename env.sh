export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-$HOME/.local/ps5-payload-sdk}"
export LLVM_CONFIG="${LLVM_CONFIG:-/opt/homebrew/opt/llvm@18/bin/llvm-config}"
export PS5_PORT="${PS5_PORT:-9021}"
[ -f "$(dirname "${BASH_SOURCE[0]:-$0}")/env.local.sh" ] && . "$(dirname "${BASH_SOURCE[0]:-$0}")/env.local.sh"
