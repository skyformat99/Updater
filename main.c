#include <stdint.h>
#include <stdio.h>

#ifndef _WIN32_IE
#define _WIN32_IE 0x0800
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0600

#include "utils.h"
#include "resource.h"

#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <process.h>
#include <shlwapi.h>
#include <versionhelpers.h>

#include <stdint.h>
#include <sodium.h>

#define APPENDED_VERSION_LENGTH (6 + 2)
#define VERSION 3

#define NUMBER_UPDATE_HOSTS 1
static const char *TOX_DOWNNLOAD_HOSTS[NUMBER_UPDATE_HOSTS] = {
    "downloads.utox.io",
};

#define SELF_UPDATER_FILE_NAME "winselfpdate"
#define VERSION_FILE_NAME "version1"
static char GET_NAME[] = "win32-latest";

#define TOX_VERSION_NAME_MAX_LEN 32

#define UTOX_TITLE "uTox"
#define TOX_EXE_NAME "uTox.exe"
#define TOX_VERSION_FILENAME "version"
#define TOX_UPDATER_FILENAME "utox_runner.exe"

#define TOX_UNINSTALL_FILENAME "uninstall.bat"
#define TOX_UNINSTALL_CONTENTS "cd %~dp0\n" TOX_UPDATER_FILENAME " --uninstall\nIF NOT EXIST uTox.exe del "\
                               "utox_runner.exe\nIF NOT EXIST uTox.exe del uninstall.bat & exit\nexit\n"

static const uint8_t TOX_SELF_PUBLIC_KEY[crypto_sign_ed25519_PUBLICKEYBYTES] = {
    0x88, 0x90, 0x5F, 0x29, 0x46, 0xBE, 0x7C, 0x4B, 0xBD, 0xEC, 0xE4, 0x67, 0x14, 0x9C, 0x1D, 0x78,
    0x48, 0xF4, 0xBC, 0x4F, 0xEC, 0x1A, 0xD1, 0xAD, 0x6F, 0x97, 0x78, 0x6E, 0xFE, 0xF3, 0xCD, 0xA1
};

static const uint8_t TOX_SELF_PUBLIC_UPDATE_KEY[crypto_sign_ed25519_PUBLICKEYBYTES] = {
    0x52, 0xA7, 0x9B, 0xCA, 0x48, 0x35, 0xD6, 0x34, 0x5E, 0x7D, 0xEF, 0x8B, 0x97, 0xC3, 0x54, 0x2D,
    0x37, 0x9A, 0x9A, 0x8B, 0x00, 0xEB, 0xF3, 0xA8, 0xAD, 0x03, 0x92, 0x3E, 0x0E, 0x50, 0x77, 0x58
};


static char tox_version_name[TOX_VERSION_NAME_MAX_LEN];

static char tox_updater_path[MAX_PATH];
static uint32_t tox_updater_path_len;

static bool is_tox_installed;
static bool is_tox_set_start_on_boot;

// Called arguments
PSTR my_cmd_args;
HINSTANCE my_hinstance;

// Common UI
static HWND main_window;
static HWND progressbar;
static HWND status_label;

void set_download_progress(int progress) {
    if (progressbar) {
        PostMessage(progressbar, PBM_SETPOS, progress, 0);
    }
}

void set_current_status(char *status) {
    SetWindowText(status_label, status);
}

static void init_tox_version_name() {
    FILE *version_file = fopen(TOX_VERSION_FILENAME, "rb");

    if (version_file) {
        int len = fread(tox_version_name, 1, sizeof(tox_version_name) - 1, version_file);
        tox_version_name[len] = 0;
        fclose(version_file);

        is_tox_installed = 1;
    }
}

#define UTOX_UPDATER_PARAM " --no-updater"
#define UTOX_SET_START_ON_BOOT_PARAM " --set=start-on-boot"

static void open_utox_and_exit() {
    char str[strlen(my_cmd_args) + sizeof(UTOX_UPDATER_PARAM) + sizeof(UTOX_SET_START_ON_BOOT_PARAM)];
    strcpy(str, my_cmd_args);
    strcat(str, UTOX_UPDATER_PARAM);

    if (is_tox_set_start_on_boot) {
        strcat(str, UTOX_SET_START_ON_BOOT_PARAM);
    }

    // FIXME CloseHandle(utox_mutex_handle);
    ShellExecute(NULL, "open", TOX_EXE_NAME, str, NULL, SW_SHOW);

    fclose(LOG_FILE);
    exit(0);
}

static void restart_updater() {
    // FIXME CloseHandle(utox_mutex_handle);
    ShellExecute(NULL, "open", tox_updater_path, my_cmd_args, NULL, SW_SHOW);

    fclose(LOG_FILE);
    exit(0);
}

static char* download_new_updater(uint32_t *new_updater_len) {
    char *new_updater = download_loop_all_host_ips(1, TOX_DOWNNLOAD_HOSTS, NUMBER_UPDATE_HOSTS, SELF_UPDATER_FILE_NAME,
                                                   strlen(SELF_UPDATER_FILE_NAME), new_updater_len, 1024 * 1024 * 4,
                                                   TOX_SELF_PUBLIC_UPDATE_KEY, 0, 0);

    return new_updater;
}

static bool install_new_updater(void *new_updater_data, uint32_t new_updater_data_len) {
    char new_path[MAX_PATH] = {0};
    FILE *file;

    memcpy(new_path, tox_updater_path, tox_updater_path_len);
    strcat(new_path, ".old");

    DeleteFile(new_path);
    MoveFile(tox_updater_path, new_path);

    file = fopen(tox_updater_path, "wb");
    if (!file) {
        LOG_TO_FILE("failed to write new updater");
        return 0;
    }

    fwrite(new_updater_data, 1, new_updater_data_len, file);

    fclose(file);
    return 1;
}

/* return 0 on success.
 * return -1 if could not write file.
 * return -2 if download failed.
 */
static int download_and_install_new_utox_version() {
    FILE *file;
    void *new_version_data;
    uint32_t len, rlen;
    new_version_data = download_loop_all_host_ips(1, TOX_DOWNNLOAD_HOSTS, NUMBER_UPDATE_HOSTS, GET_NAME,
                                                  strlen(GET_NAME), &len, 1024 * 1024 * 4, TOX_SELF_PUBLIC_KEY,
                                                  tox_version_name, APPENDED_VERSION_LENGTH);

    if (!new_version_data) {
        LOG_TO_FILE("download failed\n");
        if (is_tox_installed) {
            open_utox_and_exit();
        }

        return -2;
    }

    LOG_TO_FILE("Inflated size: %u\n", len);

    /* delete old version if found */
    file = fopen(TOX_VERSION_FILENAME, "rb");
    if (file) {
        char old_name[32];
        rlen = fread(old_name, 1, sizeof(old_name) - 1, file);
        old_name[rlen] = 0;

        /* Only there for smooth update from old updater. */
        DeleteFile(old_name);
        fclose(file);
    }

    /* write file */
    file = fopen(TOX_EXE_NAME, "wb");
    if (!file) {
        LOG_TO_FILE("fopen failed\n");
        free(new_version_data);
        return -1;
    }

    rlen = fwrite(new_version_data, 1, len, file);
    fclose(file);
    free(new_version_data);
    if (rlen != len) {
        LOG_TO_FILE("write failed (%u)\n", rlen);
        return -1;
    }

    /* write version to file */
    file = fopen(TOX_VERSION_FILENAME, "wb");
    if (file) {
        rlen = fwrite(tox_version_name, 1, APPENDED_VERSION_LENGTH, file);
        fclose(file);
        if (rlen != APPENDED_VERSION_LENGTH) {
            return -1;
        }

        return 0;
    }

    return -1;
}

static int check_new_version() {
    FILE *file;
    char *new_version_data;
    uint32_t len;

    new_version_data = download_loop_all_host_ips(0, TOX_DOWNNLOAD_HOSTS, NUMBER_UPDATE_HOSTS, VERSION_FILE_NAME,
                                                  strlen(VERSION_FILE_NAME), &len, 7 + 4, TOX_SELF_PUBLIC_KEY, 0, 0);

    if (!new_version_data) {
        LOG_TO_FILE("version download failed\n");
        return -1;
    }

    if (len != 7 + 4) {
        LOG_TO_FILE("invalid version length (%u)\n", len);
        free(new_version_data);
        return -1;
    }

    char str[7];
    memcpy(str, new_version_data + 4, 7);

    if (str[6] > VERSION + '0') {
        LOG_TO_FILE("new updater version available (%u)\n", str[6]);

        char *new_updater_data;
        uint32_t new_updater_data_len;

        new_updater_data = download_new_updater(&new_updater_data_len);

        if (!new_updater_data) {
            LOG_TO_FILE("self update download failed\n");
        } else {
            if (install_new_updater(new_updater_data, new_updater_data_len)) {
                LOG_TO_FILE("successful self update\n");

                free(new_version_data);

                restart_updater();
            }
        }
    }

    str[6] = 0;

    LOG_TO_FILE("Version: %s\n", str);
    free(new_version_data);

    if (memcmp(tox_version_name + 2, str, 6) == 0) {
        /* check if we already have the exe */
        file = fopen(TOX_EXE_NAME, "rb");
        if (!file) {
            LOG_TO_FILE("We don't have the file\n");
            fclose(file);
            return 1;
        }

        return 0;
    }

    memcpy(tox_version_name + 2, str, 7);
    return 1;
}

static int write_uninstall() {
    FILE *file = fopen(TOX_UNINSTALL_FILENAME, "wb");

    if (!file) {
        return -1;
    }

    int len = fwrite(TOX_UNINSTALL_CONTENTS, 1, sizeof(TOX_UNINSTALL_CONTENTS) - 1, file);

    fclose(file);
    if (len != sizeof(TOX_UNINSTALL_CONTENTS) - 1) {
        return -1;
    }

    return 0;
}

/* return 0 on success.
 * return -1 if could not write file.
 * return -2 if download failed.
 */
static int install_tox( int create_desktop_shortcut,
                        int create_startmenu_shortcut,
                        int use_with_tox_url,
                        wchar_t *install_path,
                        int install_path_len )
{
    char dir[MAX_PATH];

    wchar_t selfpath[MAX_PATH];
    GetModuleFileNameW(my_hinstance, selfpath, MAX_PATH);

    SHCreateDirectoryExW(NULL, install_path, NULL);
    SetCurrentDirectoryW(install_path);
    if (CopyFileW(selfpath, L""TOX_UPDATER_FILENAME, 0) == 0) {
        return -1;
    }

    int ret = write_uninstall();
    if (ret != 0) {
        return ret;
    }

    set_current_status("downloading and installing tox...");

    ret = download_and_install_new_utox_version();
    if (ret != 0) {
        return ret;
    }

    HRESULT hr;
    HKEY key;

    if (create_desktop_shortcut || create_startmenu_shortcut) {
        hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            //start menu
            IShellLink* psl;

            // Get a pointer to the IShellLink interface. It is assumed that CoInitialize
            // has already been called.
            hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLink, (LPVOID*)&psl);
            if (SUCCEEDED(hr)) {
                IPersistFile* ppf;

                // Set the path to the shortcut target and add the description.

                GetCurrentDirectory(MAX_PATH, dir);
                psl->lpVtbl->SetWorkingDirectory(psl, dir);
                strcat(dir, "\\"TOX_UPDATER_FILENAME);
                psl->lpVtbl->SetPath(psl, dir);
                psl->lpVtbl->SetDescription(psl, "Tox");

                // Query IShellLink for the IPersistFile interface, used for saving the
                // shortcut in persistent storage.
                hr = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile, (LPVOID*)&ppf);

                if (SUCCEEDED(hr)) {
                    wchar_t wsz[MAX_PATH + 64];
                    if (create_startmenu_shortcut) {
                        hr = SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, 0, wsz);
                        if (SUCCEEDED(hr)) {
                            LOG_TO_FILE("%ls\n", wsz);
                            wcscat(wsz, L"\\Programs\\Tox.lnk");
                            hr = ppf->lpVtbl->Save(ppf, wsz, TRUE);
                        }
                    }

                    if (create_desktop_shortcut) {
                        hr = SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, wsz);
                        if (SUCCEEDED(hr)) {
                            wcscat(wsz, L"\\Tox.lnk");
                            hr = ppf->lpVtbl->Save(ppf, wsz, TRUE);
                        }
                    }

                    ppf->lpVtbl->Release(ppf);
                }
                psl->lpVtbl->Release(psl);
            }
        }
    }

    if (use_with_tox_url) {
        GetCurrentDirectory(MAX_PATH, dir);
        strcat(dir, "\\" TOX_EXE_NAME);

        char str[MAX_PATH];

        if (RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\Classes\\tox", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS) {
            LOG_TO_FILE("nice\n");
            RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE*)"URL:Tox Protocol", sizeof("URL:Tox Protocol"));
            RegSetValueEx(key, "URL Protocol", 0, REG_SZ, (BYTE*)"", sizeof(""));

            HKEY key2;
            if (RegCreateKeyEx(key, "DefaultIcon", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key2, NULL) == ERROR_SUCCESS) {
                int i = sprintf(str, "%s,101", dir) + 1;
                RegSetValueEx(key2, NULL, 0, REG_SZ, (BYTE*)str, i);
            }

            if (RegCreateKeyEx(key, "shell", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key2, NULL) == ERROR_SUCCESS) {
                if (RegCreateKeyEx(key2, "open", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS) {
                    if (RegCreateKeyEx(key, "command", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key2, NULL) == ERROR_SUCCESS) {
                        int i = sprintf(str, "%s %%1", dir) + 1;
                        RegSetValueEx(key2, NULL, 0, REG_SZ, (BYTE*)str, i);
                    }
                }
            }
        }
    }

    if (RegCreateKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\uTox", 0, NULL, 0,
                       KEY_ALL_ACCESS, NULL, &key, NULL) == ERROR_SUCCESS)
        {
            wchar_t icon[install_path_len + 64];
            wchar_t uninstall[install_path_len + 64];
            memcpy(icon, install_path, install_path_len * 2);
            icon[install_path_len] = 0;
            uninstall[0] = 0;
            wcscat(uninstall, L"cmd /C start \"\" /MIN \"");

            wcscat(icon, L"\\uTox.exe");
            wcscat(uninstall, install_path);
            wcscat(uninstall, L"\\uninstall.bat\"");

            RegSetValueEx(key, NULL, 0, REG_SZ, (BYTE*)"", sizeof(""));
            RegSetValueEx(key, "DisplayName", 0, REG_SZ, (BYTE*)"uTox", sizeof("uTox"));
            RegSetValueExW(key, L"InstallLocation", 0, REG_SZ, (BYTE*)install_path, wcslen(install_path) * 2);
            RegSetValueExW(key, L"DisplayIcon", 0, REG_SZ, (BYTE*)icon, wcslen(icon) * 2);
            RegSetValueExW(key, L"UninstallString", 0, REG_SZ, (BYTE*)uninstall, wcslen(uninstall) * 2);
    }
    return 0;
}

static int uninstall_tox() {
    if (MessageBox(NULL, "Are you sure you want to uninstall uTox?", "uTox Updater", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES) {
        wchar_t wsz[MAX_PATH + 64];

        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_STARTMENU, NULL, 0, wsz))) {
            wcscat(wsz, L"\\Programs\\Tox.lnk");
            DeleteFileW(wsz);
        }

        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, wsz))) {
            wcscat(wsz, L"\\Tox.lnk");
            DeleteFileW(wsz);
        }

        SHDeleteKey(HKEY_CURRENT_USER, "Software\\Classes\\tox");
        SHDeleteKey(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\uTox");
        SHDeleteValue(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", "uTox");
        DeleteFile(TOX_EXE_NAME);
        DeleteFile(TOX_VERSION_FILENAME);
        MessageBox(main_window, "uTox uninstalled.", "uTox Updater", MB_OK | MB_SETFOREGROUND);
    }

    exit(0);
}

#define UTOX_INSTALL_ENDED 18273

static void buttons_enable(bool enable) {
    Button_Enable(GetDlgItem(main_window, ID_INSTALL_BUTTON),               enable);
    Button_Enable(GetDlgItem(main_window, ID_BROWSE_BUTTON),                enable);
    Button_Enable(GetDlgItem(main_window, ID_DESKTOP_SHORTCUT_CHECKBOX),    enable);
    Button_Enable(GetDlgItem(main_window, ID_STARTMENU_SHORTCUT_CHECKBOX),  enable);
    Button_Enable(GetDlgItem(main_window, ID_TOX_URL_CHECKBOX),             enable);
}

static void start_installation() {
    HWND desktop_shortcut_checkbox = GetDlgItem(main_window, ID_DESKTOP_SHORTCUT_CHECKBOX);
    HWND startmenu_shortcut_checkbox = GetDlgItem(main_window, ID_STARTMENU_SHORTCUT_CHECKBOX);
    HWND start_on_boot_checkbox = GetDlgItem(main_window, ID_START_ON_BOOT_CHECKBOX);
    HWND tox_url_checkbox = GetDlgItem(main_window, ID_TOX_URL_CHECKBOX);
    HWND browse_textbox = GetDlgItem(main_window, ID_BROWSE_TEXTBOX);

    bool create_desktop_shortcut, create_startmenu_shortcut, use_with_tox_url;

    wchar_t install_path[MAX_PATH];
    int install_path_len = GetWindowTextW(browse_textbox, install_path, MAX_PATH);

    if (install_path_len == 0) {
        MessageBox(main_window, "Please select a folder to install uTox in", "Error", MB_OK | MB_SETFOREGROUND);
        PostMessage(main_window, WM_APP, UTOX_INSTALL_ENDED, 0);
        return;
    }

    create_desktop_shortcut = Button_GetCheck(desktop_shortcut_checkbox);
    create_startmenu_shortcut = Button_GetCheck(startmenu_shortcut_checkbox);
    use_with_tox_url = Button_GetCheck(tox_url_checkbox);
    is_tox_set_start_on_boot = Button_GetCheck(start_on_boot_checkbox);

    LOG_TO_FILE("will install with options: %u %u %u %ls\n", create_desktop_shortcut, create_startmenu_shortcut, use_with_tox_url, install_path);

    if (MessageBox(main_window, "Are you sure you want to continue?", "uTox Updater", MB_YESNO | MB_SETFOREGROUND) != IDYES) {
        PostMessage(main_window, WM_APP, UTOX_INSTALL_ENDED, 0);
        return;
    }

    buttons_enable(0);
    int ret = install_tox(create_desktop_shortcut, create_startmenu_shortcut, use_with_tox_url, install_path, install_path_len);
    if (ret == 0) {
        set_current_status("installation complete");

        MessageBox(main_window, "Installation successful.", "uTox Updater", MB_OK | MB_SETFOREGROUND);
        open_utox_and_exit();
    } else if (ret == -1) {
        set_current_status("could not write to install directory.");
    } else if (ret == -2) {
        set_current_status("download error, please check your internet connection and try again.");
    } else {
        set_current_status("error during installation");

        MessageBox(main_window, "Installation failed. If it's not an internet issue please send the log file (tox_log.txt) to the developers.",
                                "uTox Updater", MB_OK | MB_SETFOREGROUND);
        exit(0);
    }

    PostMessage(main_window, WM_APP, UTOX_INSTALL_ENDED, 0);
    buttons_enable(1);
}

static void set_utox_path(wchar_t *path) {
    HWND browse_textbox = GetDlgItem(main_window, ID_BROWSE_TEXTBOX);

    unsigned int str_len = wcslen(path);
    if (str_len != 0) {
        wchar_t file_path[str_len + sizeof(L"\\uTox")];
        memcpy(file_path, path, str_len * sizeof(wchar_t));
        memcpy(file_path + str_len, L"\\uTox", sizeof(L"\\uTox"));
        SetWindowTextW(browse_textbox, file_path);
    }
}

static void browse_for_install_folder() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    IFileOpenDialog *pFileOpen;
    hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileOpenDialog, (void*)&pFileOpen);
    if (SUCCEEDED(hr)) {
        hr = pFileOpen->lpVtbl->SetOptions(pFileOpen, FOS_PICKFOLDERS);
        hr = pFileOpen->lpVtbl->SetTitle(pFileOpen, L"Tox Install Location");
        hr = pFileOpen->lpVtbl->Show(pFileOpen, NULL);

        if (SUCCEEDED(hr)) {
            IShellItem *pItem;
            hr = pFileOpen->lpVtbl->GetResult(pFileOpen, &pItem);

            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath;
                hr = pItem->lpVtbl->GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath);

                if (SUCCEEDED(hr)) {
                    set_utox_path(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                }
                pItem->lpVtbl->Release(pItem);
            }
        }
        pFileOpen->lpVtbl->Release(pFileOpen);

        CoUninitialize();
    } else {
        wchar_t path[MAX_PATH];
        BROWSEINFOW bi = {
            .pszDisplayName = path,
            .lpszTitle = L"Install Location",
            .ulFlags = BIF_USENEWUI | BIF_NONEWFOLDERBUTTON,
        };
        LPITEMIDLIST lpItem = SHBrowseForFolderW(&bi);
        if (!lpItem) {
            return;
        }

        SHGetPathFromIDListW(lpItem, path);
        set_utox_path(path);
    }
}

static void check_updates() {
    set_current_status("fetching new version data...");

    int new_version = check_new_version();
    set_download_progress(0);

    if (new_version == -1) {
        if (!is_tox_installed) {
            MessageBox(main_window, "Error fetching latest version data. Please check your internet connection.\n\nExiting now...",
                                    "Error", MB_OK | MB_SETFOREGROUND);
            exit(2);
        } else {
            open_utox_and_exit();
        }
    }

    set_current_status("version data fetched successfully");
    Button_Enable(GetDlgItem(main_window, ID_INSTALL_BUTTON), 1);

    if (is_tox_installed) {
        if (new_version) {
            ShowWindow(main_window, SW_SHOW);
            set_current_status("found new version");

            if (MessageBox(NULL, "A new version of uTox is available.\nUpdate?", "uTox Updater",
                MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES)
                {
                    download_and_install_new_utox_version();
            }
        }

        open_utox_and_exit();
    }
}

INT_PTR CALLBACK MainDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    static bool install_thread_running = 0;

    switch (message) {
            case WM_INITDIALOG: {
                return (INT_PTR)TRUE;
            }
            case WM_CLOSE: {
                PostQuitMessage(0);
                break;
            }
            case WM_COMMAND: {
                if (HIWORD(wParam) == BN_CLICKED) {
                    switch (LOWORD(wParam)) {
                        case ID_CANCEL_BUTTON: {
                            if (MessageBox(main_window, "Are you sure you want to exit?", "uTox Updater", MB_YESNO | MB_SETFOREGROUND) == IDYES) {
                                if (is_tox_installed) {
                                    open_utox_and_exit();
                                }
                                else {
                                    exit(0);
                                }
                            }
                            break;
                        }

                        case ID_INSTALL_BUTTON: {
                            if (!install_thread_running) {
                                if (_beginthread(start_installation, 0, 0) != -1) {
                                    install_thread_running = 1;
                                }
                            }

                            break;
                        }
                        case ID_BROWSE_BUTTON: {
                            buttons_enable(0);
                            browse_for_install_folder();
                            buttons_enable(1);

                            break;
                        }
                    }
                }
                break;
            }

            case WM_APP: {
                if (wParam == UTOX_INSTALL_ENDED)
                    install_thread_running = 0;
            }
    }

    return (INT_PTR)FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR cmd, int nCmdShow) {
    if (CreateMutex(NULL, 0, UTOX_TITLE)) {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS || err == ERROR_ACCESS_DENIED) {
            /* uTox is running. */
            HWND window = FindWindow(UTOX_TITLE, NULL);
            if (window) {
                SetForegroundWindow(window);
            }
            return 0;
        }
    } else {
        exit(1); // Failed to create mutex
    }


    my_cmd_args = cmd;
    my_hinstance = hInstance;

    tox_updater_path_len = GetModuleFileName(NULL, tox_updater_path, MAX_PATH);
    tox_updater_path[tox_updater_path_len] = 0;

    {
        char path[MAX_PATH], *s;
        memcpy(path, tox_updater_path, tox_updater_path_len + 1);
        s = path + tox_updater_path_len;
        while (*s != '\\') {
            s--;
        }

        *s = 0;
        SetCurrentDirectory(path);
    }

    LPWSTR *arglist;
    int argc, i;

    init_tox_version_name();

    /* Convert PSTR command line args from windows to argc */
    arglist = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (NULL != arglist) {
        for (i = 0; i < argc; i++) {
            if (wcscmp(arglist[i], L"--uninstall") == 0) {
                if (is_tox_installed) {
                    uninstall_tox();
                    return 0;
                }
            }
        }
    }

    LOG_FILE = fopen("tox_log.txt", "w");

    /* initialize winsock */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        LOG_TO_FILE("WSAStartup failed\n");
        return 1;
    }

    if (IsWindowsVistaOrGreater()) {
        /* check if we are on a 64-bit system */
        bool iswow64 = 0;
        bool (WINAPI *fnIsWow64Process)(HANDLE, bool*)  = (void*)GetProcAddress(GetModuleHandleA("kernel32"),"IsWow64Process");
        if (fnIsWow64Process) {
            fnIsWow64Process(GetCurrentProcess(), &iswow64);
        }

        if (iswow64) {
            /* replace the arch in the GET_NAME/tox_version_name strings (todo: not use constants for offsets) */
            GET_NAME[3] = '6';
            GET_NAME[4] = '4';
            tox_version_name[0] = '6';
            tox_version_name[1] = '4';
            LOG_TO_FILE("detected 64bit system\n");
        } else {
            GET_NAME[3] = '3';
            GET_NAME[4] = '2';
            tox_version_name[0] = '3';
            tox_version_name[1] = '2';
            LOG_TO_FILE("detected 32bit system\n");
        }
    } else {
        GET_NAME[3] = 'x';
        GET_NAME[4] = 'p';
        tox_version_name[0] = 'x';
        tox_version_name[1] = 'p';
        LOG_TO_FILE("detected XP system\n");
    }

    /* init common controls */
    INITCOMMONCONTROLSEX InitCtrlEx;

    InitCtrlEx.dwSize = sizeof(INITCOMMONCONTROLSEX);
    InitCtrlEx.dwICC = ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&InitCtrlEx);

    main_window = CreateDialog(my_hinstance, MAKEINTRESOURCE(IDD_MAIN_DIALOG), NULL, MainDialogProc);

    if (!main_window) {
        LOG_TO_FILE("error creating main window %lu\n", GetLastError());
        exit(0);
    }

    progressbar = GetDlgItem(main_window, ID_PROGRESSBAR);
    set_download_progress(0);
    status_label = GetDlgItem(main_window, IDC_STATUS_LABEL);

    if (!is_tox_installed) {
        // show installer controls
        HWND desktop_shortcut_checkbox = GetDlgItem(main_window, ID_DESKTOP_SHORTCUT_CHECKBOX);
        Button_SetCheck(desktop_shortcut_checkbox, 1);
        ShowWindow(desktop_shortcut_checkbox, SW_SHOW);

        HWND startmenu_shortcut_checkbox = GetDlgItem(main_window, ID_STARTMENU_SHORTCUT_CHECKBOX);
        Button_SetCheck(startmenu_shortcut_checkbox, 1);
        ShowWindow(startmenu_shortcut_checkbox, SW_SHOW);

        HWND start_on_boot_checkbox = GetDlgItem(main_window, ID_START_ON_BOOT_CHECKBOX);
        Button_SetCheck(start_on_boot_checkbox, 1);
        ShowWindow(start_on_boot_checkbox, SW_SHOW);

        ShowWindow(GetDlgItem(main_window, ID_TOX_URL_CHECKBOX), SW_SHOW);

        wchar_t appdatalocal_path[MAX_PATH] = {0};
        if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdatalocal_path) == S_OK) {
            set_utox_path(appdatalocal_path);
        }

        Button_Enable(GetDlgItem(main_window, ID_INSTALL_BUTTON), 0);
        ShowWindow(GetDlgItem(main_window, ID_INSTALL_BUTTON), SW_SHOW);

        Edit_SetReadOnly(GetDlgItem(main_window, ID_BROWSE_TEXTBOX), 1);
        ShowWindow(GetDlgItem(main_window, ID_BROWSE_TEXTBOX), SW_SHOW);
        ShowWindow(GetDlgItem(main_window, ID_BROWSE_BUTTON), SW_SHOW);
        ShowWindow(GetDlgItem(main_window, IDC_INSTALL_FOLDER_LABEL), SW_SHOW);
        ShowWindow(main_window, SW_SHOW);
    }

    _beginthread(check_updates, 0, NULL);

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        DispatchMessage(&msg);
    }

    open_utox_and_exit();

    return 0;
}
