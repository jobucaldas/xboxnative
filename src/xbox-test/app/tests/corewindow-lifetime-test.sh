#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
source="$here/main.cpp"
grep -Fq 'winrt::event_token m_windowActivatedToken{}' "$source"
grep -Fq 'winrt::event_token m_acceleratorToken{}' "$source"
grep -Fq 'winrt::event_token m_backRequestedToken{}' "$source"
grep -Fq 'void revoke_window_events() noexcept' "$source"
grep -Fq 'm_window.Activated(m_windowActivatedToken)' "$source"
grep -Fq 'm_window.Closed(m_windowClosedToken)' "$source"
grep -Fq 'm_window.Dispatcher().AcceleratorKeyActivated(m_acceleratorToken)' "$source"
grep -Fq 'SystemNavigationManager::GetForCurrentView().BackRequested(m_backRequestedToken)' "$source"
grep -Fq 'void Uninitialize() { revoke_window_events(); m_input.release_all();' "$source"
grep -Fq '~App() { revoke_window_events(); m_input.release_all(); }' "$source"
printf '%s\n' 'COREWINDOW LIFETIME PASS: all input event tokens are revoked before App input teardown'
