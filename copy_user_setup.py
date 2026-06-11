# Skrypt do kopiowania User_Setup.h do biblioteki TFT_eSPI w PlatformIO
# Uruchamiany przed kompilacją

Import("env")
import os
import shutil

# Ścieżka do User_Setup.h w projekcie
user_setup_src = os.path.join(env["PROJECT_DIR"], "User_Setup.h")

# Ścieżka do biblioteki TFT_eSPI
libdeps_path = os.path.join(env["PROJECT_DIR"], ".pio", "libdeps", env["PIOENV"], "TFT_eSPI")
user_setup_dst = os.path.join(libdeps_path, "User_Setup.h")
user_setup_select_dst = os.path.join(libdeps_path, "User_Setup_Select.h")

# Sprawdź czy biblioteka istnieje
if os.path.exists(libdeps_path):
    # Skopiuj User_Setup.h
    if os.path.exists(user_setup_src):
        shutil.copy2(user_setup_src, user_setup_dst)
        print(f"Copied User_Setup.h to {user_setup_dst}")

        # Wymuś korzystanie tylko z User_Setup.h
        # Minimal User_Setup_Select that still pulls in the driver command defines
        select_content = """#pragma once
    #define USER_SETUP_LOADED
    #include \"User_Setup.h\"

    // Wciągnij definicje poleceń dla wybranego sterownika
    #if defined(ILI9341_DRIVER) || defined(ILI9341_2_DRIVER) || defined(ILI9342_DRIVER)
    #define TFT_DRIVER 0x9341
    #include \"TFT_Drivers/ILI9341_Defines.h\"
    #endif

    #if defined(ST7789_DRIVER) || defined(ST7789_2_DRIVER)
    #define TFT_DRIVER 0x7789
    #include \"TFT_Drivers/ST7789_Defines.h\"
    #endif
    """
        with open(user_setup_select_dst, "w", encoding="utf-8") as f:
            f.write(select_content)
        print(f"Wrote minimal User_Setup_Select.h to {user_setup_select_dst}")
    else:
        print(f"Warning: {user_setup_src} not found")
else:
    print(f"Warning: TFT_eSPI library not found at {libdeps_path}")
    print("Library will be downloaded during build, then User_Setup.h will be copied")