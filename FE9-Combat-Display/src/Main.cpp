#include <SDL/SDL_image.h>
#include <SDL/SDL.h>
#include <SDL/SDL_syswm.h>

#include <Windows.h>
#include <tlhelp32.h>
#include <Psapi.h>

#include <cstdio>
#include <cstring>
#include <string>

#define u8 unsigned char
#define u64 unsigned long long

SDL_Window* sdlWindow = nullptr;
SDL_Renderer* sdlRenderer = nullptr;

SDL_Texture* imgBase1 = nullptr;
SDL_Texture* imgBase2 = nullptr;
SDL_Texture* imgNumbers[10];

const u64 ARAM_FAKESIZE = 0x02000000LL;
const u64 ARAM_START = 0x7e000000LL;
const u64 ARAM_END = 0x7f000000LL;
const u64 MEM1_START = 0x80000000LL;
const u64 MEM1_END = 0x81800000LL;
const u64 MEM2_START = 0x90000000LL;
const u64 MEM2_END = 0x94000000LL;

u8 phase = 2;    // 0 during player phase, 1 during enemy phase
u8 inBattle = 0;    // 127 or 255 during combat
u8 battleSide = 0;    // determines which side (left or right) the blue unit is on
u8 attackingUnitAtk = 100;
u8 attackingUnitDef = 100;
u8 attackingUnitHit = 100;
u8 attackingUnitCrt = 100;
u8 defendingUnitAtk = 100;
u8 defendingUnitDef = 100;
u8 defendingUnitHit = 100;
u8 defendingUnitCrt = 100;

enum Region
{
    JPN,
    USA,
    PAL
};

const int textWidth = 19;
const int textHeight = 26;
// renders the number centered around the x and y
void renderNumber(int num, int x, int y);

DWORD dolphinPID = NULL;
HANDLE dolphinHandle = NULL;
int tryAgain = 0;

u64 m_emuRAMAddressStart = 0;
u64 m_emuARAMAdressStart = 0;
u64 m_MEM2AddressStart = 0;
bool m_MEM2Present = false;
bool m_ARAMAccessible = false;

std::string pathToEXE = "";

void attachToDolphin();
void updateDisplay();
bool obtainEmuRAMInformations();
void setIcon();

int main(int argc, char* argv[])
{
    // Grab the path to the .exe to load images from the local res folder
    if (argc > 0)
    {
        std::string path = argv[0];

        if (path.size() > 5)
        {
            std::string end = path.substr(path.size() - 4, 4);

            if (end[0] == '.' &&
                end[1] == 'e' &&
                end[2] == 'x' &&
                end[3] == 'e')
            {
                size_t idx = path.find_last_of('\\', path.size());

                if (idx != std::string::npos)
                {
                    pathToEXE = path.substr(0, idx + 1);
                }
            }
        }
    }

    printf("Path to .exe = '%s'\n", pathToEXE.c_str());

    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);

    sdlWindow = SDL_CreateWindow("FE9 Combat Display", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 97 * 2, 80 * 2, SDL_WINDOW_SHOWN);
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED); //| SDL_RENDERER_PRESENTVSYNC);

    // Disable the minimize and maximize buttons.
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(sdlWindow, &wmInfo);
    HWND hwnd = wmInfo.info.win.window;
    SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MINIMIZEBOX);
    SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_MAXIMIZEBOX);

    setIcon();

    SDL_SetRenderTarget(sdlRenderer, nullptr);

    imgBase1      = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/Display5.png"      ).c_str());
    imgBase2      = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/Display6.png"      ).c_str());
    imgNumbers[0] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/0.png").c_str());
    imgNumbers[1] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/1.png").c_str());
    imgNumbers[2] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/2.png").c_str());
    imgNumbers[3] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/3.png").c_str());
    imgNumbers[4] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/4.png").c_str());
    imgNumbers[5] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/5.png").c_str());
    imgNumbers[6] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/6.png").c_str());
    imgNumbers[7] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/7.png").c_str());
    imgNumbers[8] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/8.png").c_str());
    imgNumbers[9] = IMG_LoadTexture(sdlRenderer, (pathToEXE + "res/NumbersSmall/9.png").c_str());

    SDL_SetRenderDrawColor(sdlRenderer, 255, 0, 255, 255);

    bool running = true;
    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_QUIT:
            {
                running = false;
                break;
            }

            default:
                break;
            }
        }

        if (dolphinHandle != NULL && dolphinPID != NULL)
        {
            updateDisplay();
        }
        else
        {
            phase = 2;

            attachToDolphin();
        }

        SDL_RenderClear(sdlRenderer);

        if ((inBattle == 127 || inBattle == 255) && battleSide != 0)
        {
            u8 leftAtk = 0;
            u8 leftHit = 0;
            u8 leftCrt = 0;
            u8 rightAtk = 0;
            u8 rightHit = 0;
            u8 rightCrt = 0;

            bool unknown = false;

            bool playerIsOnLeft = true;

            if (phase == 0 || phase == 2) //player phase or green unit phase
            {
                if (battleSide == 112 || battleSide == 160 || battleSide == 48) //blue is on the left side of the screen
                {
                    leftAtk = attackingUnitAtk - defendingUnitDef;
                    leftHit = attackingUnitHit;
                    leftCrt = attackingUnitCrt;
                    rightAtk = defendingUnitAtk - attackingUnitDef;
                    rightHit = defendingUnitHit;
                    rightCrt = defendingUnitCrt;
                    playerIsOnLeft = true;

                    if (attackingUnitAtk < defendingUnitDef)
                    {
                        leftAtk = 0;
                    }

                    if (defendingUnitAtk < attackingUnitDef)
                    {
                        rightAtk = 0;
                    }
                }
                else //if (battleSide == 80 || battleSide == 128 || battleSide == 16) //blue is on right side of the screen
                {
                    leftAtk = defendingUnitAtk - attackingUnitDef;
                    leftHit = defendingUnitHit;
                    leftCrt = defendingUnitCrt;
                    rightAtk = attackingUnitAtk - defendingUnitDef;
                    rightHit = attackingUnitHit;
                    rightCrt = attackingUnitCrt;
                    playerIsOnLeft = false;

                    if (defendingUnitAtk < attackingUnitDef)
                    {
                        leftAtk = 0;
                    }

                    if (attackingUnitAtk < defendingUnitDef)
                    {
                        rightAtk = 0;
                    }
                }
                //else
                //{
                //    printf("battleSide was unexpected value on player phase%d\n", battleSide);
                //    unknown = true;
                //}
            }
            else if (phase == 1) //enemy phase
            {
                if (battleSide == 128 || battleSide == 16 || battleSide == 80) //blue is on the left side of the screen
                {
                    leftAtk = defendingUnitAtk - attackingUnitDef;
                    leftHit = defendingUnitHit;
                    leftCrt = defendingUnitCrt;
                    rightAtk = attackingUnitAtk - defendingUnitDef;
                    rightHit = attackingUnitHit;
                    rightCrt = attackingUnitCrt;
                    playerIsOnLeft = true;

                    if (defendingUnitAtk < attackingUnitDef)
                    {
                        leftAtk = 0;
                    }

                    if (attackingUnitAtk < defendingUnitDef)
                    {
                        rightAtk = 0;
                    }
                }
                else //if (battleSide == 160 || battleSide == 48 || battleSide == 112) //blue is on right side of the screen
                {
                    leftAtk = attackingUnitAtk - defendingUnitDef;
                    leftHit = attackingUnitHit;
                    leftCrt = attackingUnitCrt;
                    rightAtk = defendingUnitAtk - attackingUnitDef;
                    rightHit = defendingUnitHit;
                    rightCrt = defendingUnitCrt;
                    playerIsOnLeft = false;

                    if (attackingUnitAtk < defendingUnitDef)
                    {
                        leftAtk = 0;
                    }

                    if (defendingUnitAtk < attackingUnitDef)
                    {
                        rightAtk = 0;
                    }
                }
                //else
                //{
                //    printf("battleSide was unexpected value on enemy phase%d\n", battleSide);
                //    unknown = true;
                //}
            }
            else
            {
                printf("unknown phase %d\n", phase);
                unknown = true;
            }

            if (!unknown)
            {
                if (playerIsOnLeft)
                {
                    SDL_RenderCopy(sdlRenderer, imgBase1, nullptr, nullptr);
                }
                else
                {
                    SDL_RenderCopy(sdlRenderer, imgBase2, nullptr, nullptr);
                }
                renderNumber(leftHit, 19 * 2, (14 + 15) * 2);
                renderNumber(leftAtk, 19 * 2, (34 + 15) * 2);
                renderNumber(leftCrt, 19 * 2, (51 + 15) * 2);

                renderNumber(rightHit, 73 * 2, (15 + 15) * 2);
                renderNumber(rightAtk, 73 * 2, (34 + 15) * 2);
                renderNumber(rightCrt, 73 * 2, (51 + 15) * 2);
            }
        }

        SDL_RenderPresent(sdlRenderer);

        // we really dont need to update the window very often. its almost always the same image as last time.
        SDL_Delay(30);
    }

    return 0;
}

//https://caedesnotes.wordpress.com/2015/04/13/how-to-integrate-your-sdl2-window-icon-or-any-image-into-your-executable/
// Sets the SDL window icon from data within the exe instead of loading from a file.
void setIcon()
{
#include "Icon.cpp"

    Uint32 rmask, gmask, bmask, amask;
    rmask = 0x000000ff;
    gmask = 0x0000ff00;
    bmask = 0x00ff0000;
    amask = (imgIcon.bytes_per_pixel == 3) ? 0 : 0xff000000;

    SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(
        (void*)imgIcon.pixel_data,
        imgIcon.width,
        imgIcon.height,
        imgIcon.bytes_per_pixel * 8,
        imgIcon.bytes_per_pixel * imgIcon.width,
        rmask,
        gmask,
        bmask,
        amask);

    SDL_SetWindowIcon(sdlWindow, icon);

    SDL_FreeSurface(icon);
}

// renders a number (max 3 digits), centered at (x,y)
void renderNumber(int num, int x, int y)
{
    int d = num;

    if (d < 10)
    {
        SDL_Rect rec1 = { (x - textWidth / 2), (y - textHeight / 2), textWidth, textHeight };

        SDL_RenderCopy(sdlRenderer, imgNumbers[d], nullptr, &rec1);
    }
    else if (d < 100)
    {
        SDL_Rect rec1 = { (x - textWidth), (y - textHeight / 2), textWidth, textHeight };
        SDL_Rect rec2 = { (x + 0), (y - textHeight / 2), textWidth, textHeight };

        SDL_RenderCopy(sdlRenderer, imgNumbers[d / 10], nullptr, &rec1);
        SDL_RenderCopy(sdlRenderer, imgNumbers[d % 10], nullptr, &rec2);
    }
    else
    {
        SDL_Rect rec1 = { (x - (3 * textWidth) / 2), (y - textHeight / 2), textWidth, textHeight };
        SDL_Rect rec2 = { (x - textWidth / 2), (y - textHeight / 2), textWidth, textHeight };
        SDL_Rect rec3 = { (x + textWidth / 2), (y - textHeight / 2), textWidth, textHeight };

        SDL_RenderCopy(sdlRenderer, imgNumbers[d / 100], nullptr, &rec1);
        SDL_RenderCopy(sdlRenderer, imgNumbers[(d / 10) % 10], nullptr, &rec2);
        SDL_RenderCopy(sdlRenderer, imgNumbers[d % 10], nullptr, &rec3);
    }
}

// Gets the process id from the given process name.
DWORD getPIDByName(const char* processName)
{
    PROCESSENTRY32 pe32 = { 0 };
    HANDLE hSnapshot = NULL;

    pe32.dwSize = sizeof(PROCESSENTRY32);
    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    bool foundProcess = false;

    if (Process32First(hSnapshot, &pe32))
    {
        do
        {
            if (strcmp(pe32.szExeFile, processName) == 0)
            {
                foundProcess = true;
                break;
            }
        } while (Process32Next(hSnapshot, &pe32));
    }

    if (foundProcess && hSnapshot != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hSnapshot);
        return pe32.th32ProcessID;
    }

    return NULL;
}

void attachToDolphin()
{
    dolphinPID = NULL;
    dolphinHandle = NULL;
    m_emuRAMAddressStart = 0;
    m_emuARAMAdressStart = 0;
    m_MEM2AddressStart = 0;
    m_MEM2Present = false;
    m_ARAMAccessible = false;
    phase = 2;
    attackingUnitAtk = 0;
    attackingUnitDef = 0;
    attackingUnitHit = 0;
    attackingUnitCrt = 0;
    defendingUnitAtk = 0;
    defendingUnitDef = 0;
    defendingUnitHit = 0;
    defendingUnitCrt = 0;
    inBattle = 0;
    battleSide = 0;

    // Only attempt to connect to dolphin every 2 seconds to avoid unnessesary cpu usage.
    tryAgain--;
    if (tryAgain > 0)
    {
        return;
    }
    tryAgain = 120;

    dolphinPID = getPIDByName("Dolphin.exe");

    if (dolphinPID != NULL)
    {
        dolphinHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, false, dolphinPID);
        if (dolphinHandle == NULL)
        {
            printf("Error: Found the process, but couldn't open and get a handle.\n");
            dolphinPID = NULL;
            return;
        }

        if (!obtainEmuRAMInformations())
        {
            // Dolphin is open but isn't emulating anything yet. Close the handle and try again later.

            CloseHandle(dolphinHandle);
            dolphinPID = NULL;
            dolphinHandle = NULL;
            m_emuRAMAddressStart = 0;
            m_emuARAMAdressStart = 0;
            m_MEM2AddressStart = 0;
            m_MEM2Present = false;
            m_ARAMAccessible = false;
            phase = 2;
            attackingUnitAtk = 0;
            attackingUnitDef = 0;
            attackingUnitHit = 0;
            attackingUnitCrt = 0;
            defendingUnitAtk = 0;
            defendingUnitDef = 0;
            defendingUnitHit = 0;
            defendingUnitCrt = 0;
            inBattle = 0;
            battleSide = 0;
        }
    }
}

// magic copied from https://github.com/aldelaro5/Dolphin-memory-engine
bool obtainEmuRAMInformations()
{
    MEMORY_BASIC_INFORMATION info;
    bool MEM1Found = false;
    for (unsigned char* p = nullptr;
        VirtualQueryEx(dolphinHandle, p, &info, sizeof(info)) == sizeof(info); p += info.RegionSize)
    {
        // Check region size so that we know it's MEM2
        if (!m_MEM2Present && info.RegionSize == 0x4000000)
        {
            u64 regionBaseAddress = 0;
            std::memcpy(&regionBaseAddress, &(info.BaseAddress), sizeof(info.BaseAddress));
            if (MEM1Found && regionBaseAddress > m_emuRAMAddressStart + 0x10000000)
            {
                // In some cases MEM2 could actually be before MEM1. Once we find MEM1, ignore regions of
                // this size that are too far away. There apparently are other non-MEM2 regions of size
                break;
            }
            // View the comment for MEM1.
            PSAPI_WORKING_SET_EX_INFORMATION wsInfo;
            wsInfo.VirtualAddress = info.BaseAddress;
            if (QueryWorkingSetEx(dolphinHandle, &wsInfo, sizeof(PSAPI_WORKING_SET_EX_INFORMATION)))
            {
                if (wsInfo.VirtualAttributes.Valid)
                {
                    std::memcpy(&m_MEM2AddressStart, &(regionBaseAddress), sizeof(regionBaseAddress));
                    m_MEM2Present = true;
                }
            }
        }
        else if (info.RegionSize == 0x2000000 && info.Type == MEM_MAPPED)
        {
            // Here, it's likely the right page, but it can happen that multiple pages with these criteria
            // exists and have nothing to do with the emulated memory. Only the right page has valid
            // working set information so an additional check is required that it is backed by physical
            // memory.
            PSAPI_WORKING_SET_EX_INFORMATION wsInfo;
            wsInfo.VirtualAddress = info.BaseAddress;
            if (QueryWorkingSetEx(dolphinHandle, &wsInfo, sizeof(PSAPI_WORKING_SET_EX_INFORMATION)))
            {
                if (wsInfo.VirtualAttributes.Valid)
                {
                    if (!MEM1Found)
                    {
                        std::memcpy(&m_emuRAMAddressStart, &(info.BaseAddress), sizeof(info.BaseAddress));
                        MEM1Found = true;
                    }
                    else
                    {
                        u64 aramCandidate = 0;
                        std::memcpy(&aramCandidate, &(info.BaseAddress), sizeof(info.BaseAddress));
                        if (aramCandidate == m_emuRAMAddressStart + 0x2000000)
                        {
                            m_emuARAMAdressStart = aramCandidate;
                            m_ARAMAccessible = true;
                        }
                    }
                }
            }
        }
    }

    if (m_MEM2Present)
    {
        m_emuARAMAdressStart = 0;
        m_ARAMAccessible = false;
    }

    if (m_emuRAMAddressStart == 0)
    {
        // Here, Dolphin is running, but the emulation hasn't started
        return false;
    }

    return true;
}

//uiAddr is the address shown in the UI of dolphin memory engine
u64 getAddressOfDolphinMemoryToRead(u64 uiAddr)
{
    u64 addr = uiAddr;

    // ARAM address
    if (addr >= ARAM_START && addr < ARAM_END)
    {
        addr -= ARAM_START;
    }
    // MEM1 address
    else if (addr >= MEM1_START && addr < MEM1_END)
    {
        addr -= MEM1_START;
        if (m_ARAMAccessible)
        {
            addr += ARAM_FAKESIZE;
        }
    }
    // MEM2 address
    else if (addr >= MEM2_START && addr < MEM2_END)
    {
        addr -= MEM2_START;
        addr += (MEM2_START - MEM1_START);
    }

    u64 RAMAddress = 0;
    if (m_ARAMAccessible)
    {
        if (addr >= ARAM_FAKESIZE)
        {
            RAMAddress = m_emuRAMAddressStart + addr - ARAM_FAKESIZE;
        }
        else
        {
            RAMAddress = m_emuARAMAdressStart + addr;
        }
    }
    else if (addr >= (MEM2_START - MEM1_START))
    {
        RAMAddress = m_MEM2AddressStart + addr - (MEM2_START - MEM1_START);
    }
    else
    {
        RAMAddress = m_emuRAMAddressStart + addr;
    }

    return RAMAddress;
}

void updateDisplay()
{
    SIZE_T bytesRead = 0;
    char gameId[7];
    gameId[6] = 0;
    if ((!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(MEM1_START), (LPVOID)(&gameId), (SIZE_T)6, &bytesRead) || bytesRead != 6))
    {
        printf("Error when reading from dolphin, disconnecting...\n");
        CloseHandle(dolphinHandle);
        dolphinPID = NULL;
        dolphinHandle = NULL;
        return;
    }

    Region gameRegion;
    std::string gameIdStr = gameId;
    if (gameIdStr == "GFEJ01")
    {
        gameRegion = JPN;
    }
    else if (gameIdStr == "GFEE01")
    {
        gameRegion = USA;
    }
    else if (gameIdStr == "GFEP01")
    {
        gameRegion = PAL;
    }
    else
    {
        printf("Unknown (not PoR?) game ID: %s\n", gameId);
        CloseHandle(dolphinHandle);
        dolphinPID = NULL;
        dolphinHandle = NULL;
        return;
    }

    u64 ADDR_PHASE = 0;
    u64 ADDR_ATTACKING_UNIT_ATK = 0;
    u64 ADDR_ATTACKING_UNIT_DEF = 0;
    u64 ADDR_ATTACKING_UNIT_HIT = 0;
    u64 ADDR_ATTACKING_UNIT_CRT = 0;
    u64 ADDR_DEFENDING_UNIT_ATK = 0;
    u64 ADDR_DEFENDING_UNIT_DEF = 0;
    u64 ADDR_DEFENDING_UNIT_HIT = 0;
    u64 ADDR_DEFENDING_UNIT_CRT = 0;
    u64 ADDR_IN_BATTLE = 0;
    u64 ADDR_BATTLE_SIDE = 0;

    switch (gameRegion)
    {
    case JPN:
        ADDR_PHASE = 0x8032E799LL;
        ADDR_ATTACKING_UNIT_ATK = 0x8032F195LL;
        ADDR_ATTACKING_UNIT_DEF = 0x8032F197LL;
        ADDR_ATTACKING_UNIT_HIT = 0x8032F19FLL;
        ADDR_ATTACKING_UNIT_CRT = 0x8032F1A5LL;
        ADDR_DEFENDING_UNIT_ATK = 0x8032F475LL;
        ADDR_DEFENDING_UNIT_DEF = 0x8032F477LL;
        ADDR_DEFENDING_UNIT_HIT = 0x8032F47FLL;
        ADDR_DEFENDING_UNIT_CRT = 0x8032F485LL;
        ADDR_IN_BATTLE = 0x80603216LL;
        ADDR_BATTLE_SIDE = 0x8032FBA8LL;
        break;

    case USA:
        ADDR_PHASE = 0x80330719LL;
        ADDR_ATTACKING_UNIT_ATK = 0x80331145LL;
        ADDR_ATTACKING_UNIT_DEF = 0x80331147LL;
        ADDR_ATTACKING_UNIT_HIT = 0x8033114FLL;
        ADDR_ATTACKING_UNIT_CRT = 0x80331155LL;
        ADDR_DEFENDING_UNIT_ATK = 0x80331425LL;
        ADDR_DEFENDING_UNIT_DEF = 0x80331427LL;
        ADDR_DEFENDING_UNIT_HIT = 0x8033142FLL;
        ADDR_DEFENDING_UNIT_CRT = 0x80331435LL;
        ADDR_IN_BATTLE = 0x80603456LL;
        ADDR_BATTLE_SIDE = 0x80331B58LL;
        break;

    case PAL:
        ADDR_PHASE = 0x8033A719LL;
        ADDR_ATTACKING_UNIT_ATK = 0x8033B145LL;
        ADDR_ATTACKING_UNIT_DEF = 0x8033B147LL;
        ADDR_ATTACKING_UNIT_HIT = 0x8033B14FLL;
        ADDR_ATTACKING_UNIT_CRT = 0x8033B155LL;
        ADDR_DEFENDING_UNIT_ATK = 0x8033B425LL;
        ADDR_DEFENDING_UNIT_DEF = 0x8033B427LL;
        ADDR_DEFENDING_UNIT_HIT = 0x8033B42FLL;
        ADDR_DEFENDING_UNIT_CRT = 0x8033B435LL;
        ADDR_IN_BATTLE = 0x80641FF6LL;
        ADDR_BATTLE_SIDE = 0x8033BB58LL;
        break;
    }

    bytesRead = 0;
    if ((!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_PHASE), (LPVOID)(&phase), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_ATTACKING_UNIT_ATK), (LPVOID)(&attackingUnitAtk), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_ATTACKING_UNIT_DEF), (LPVOID)(&attackingUnitDef), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_ATTACKING_UNIT_HIT), (LPVOID)(&attackingUnitHit), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_ATTACKING_UNIT_CRT), (LPVOID)(&attackingUnitCrt), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_DEFENDING_UNIT_ATK), (LPVOID)(&defendingUnitAtk), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_DEFENDING_UNIT_DEF), (LPVOID)(&defendingUnitDef), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_DEFENDING_UNIT_HIT), (LPVOID)(&defendingUnitHit), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_DEFENDING_UNIT_CRT), (LPVOID)(&defendingUnitCrt), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_IN_BATTLE), (LPVOID)(&inBattle), (SIZE_T)1, &bytesRead) || bytesRead != 1) ||
        (!ReadProcessMemory(dolphinHandle, (LPCVOID)getAddressOfDolphinMemoryToRead(ADDR_BATTLE_SIDE), (LPVOID)(&battleSide), (SIZE_T)1, &bytesRead) || bytesRead != 1))
    {
        printf("Error when reading from dolphin, disconnecting...\n");
        CloseHandle(dolphinHandle);
        dolphinPID = NULL;
        dolphinHandle = NULL;
        m_emuRAMAddressStart = 0;
        m_emuARAMAdressStart = 0;
        m_MEM2AddressStart = 0;
        m_MEM2Present = false;
        m_ARAMAccessible = false;
        phase = 2;
        attackingUnitAtk = 0;
        attackingUnitDef = 0;
        attackingUnitHit = 0;
        attackingUnitCrt = 0;
        defendingUnitAtk = 0;
        defendingUnitDef = 0;
        defendingUnitHit = 0;
        defendingUnitCrt = 0;
        inBattle = 0;
        battleSide = 0;
        return;
    }
}
