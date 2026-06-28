@echo off
chcp 65001
cd /d "%~dp0"
powershell -Command "Start-Process powershell -ArgumentList '-Command', 'taskkill /F /IM dx11_tex_dbg.exe 2>$null; Start-Process ''%cd%\dx11_tex_dbg.exe'' -ArgumentList ''--multi -t YuanShen.exe''' -Verb RunAs"