#include <SADXModLoader.h>
#include <FunctionHook.h>

#include "Trampoline.h"

#include <chrono>
#include <unordered_map>

// Frame limiter state
static constexpr long double GAME_FRAMERATE = 60.0;
static long double game_framerate = GAME_FRAMERATE;

using TickerClock = std::chrono::steady_clock;

static std::chrono::time_point<TickerClock> ticker_last = TickerClock::now();
static long double ticker = 0.0;

// Button queue
static uint32_t buttons_queue[4];

static void PushButtonQueue()
{
    buttons_queue[0] |= ControllerPointers[0]->PressedButtons;
    buttons_queue[1] |= ControllerPointers[1]->PressedButtons;
    buttons_queue[2] |= ControllerPointers[2]->PressedButtons;
    buttons_queue[3] |= ControllerPointers[3]->PressedButtons;
}

static void ConsumeButtonQueue()
{
    ControllerPointers[0]->PressedButtons = buttons_queue[0];
    ControllerPointers[1]->PressedButtons = buttons_queue[1];
    ControllerPointers[2]->PressedButtons = buttons_queue[2];
    ControllerPointers[3]->PressedButtons = buttons_queue[3];
    buttons_queue[0] = 0;
    buttons_queue[1] = 0;
    buttons_queue[2] = 0;
    buttons_queue[3] = 0;
}

// TaskWk override
struct TaskWk_State
{
    Rotation3 rotation;
    NJS_VECTOR position;
    NJS_VECTOR scale;

    NJS_VECTOR col_center;
};

struct TaskWk
{
    int bad = 2;
    TaskWk_State state[2];
};

enum TaskSt
{
    Active,
    Track,
    Untrack
};

static std::unordered_map<EntityData1*, TaskWk> taskwk_map;
static std::unordered_map<EntityData1*, TaskSt> taskwk_st;

static void TrackTask(EntityData1 *taskwk)
{
    // Emplace to task map
    taskwk_map.emplace(std::make_pair(taskwk, TaskWk{}));
}

static void UntrackTask(EntityData1 *taskwk)
{
    // Remove from task map
    taskwk_map.erase(taskwk);
}

static void CheckTask(EntityData1 *taskwk)
{
    if (taskwk != NULL)
    {
        if (taskwk_map.find(taskwk) != taskwk_map.end())
            taskwk_st[taskwk] = TaskSt::Active;
        else
            taskwk_st[taskwk] = TaskSt::Track;
    }
}

static void CheckTasks()
{
    // Make sure our task map is correct
    for (auto &i : taskwk_st)
        taskwk_st[i.first] = TaskSt::Untrack;

    for (int i = 0; i < 8; i++)
    {
        for (ObjectMaster *current = ObjectListThing[i]; current != nullptr; current = current->Next)
            CheckTask(current->Data1);
    }
    
    for (auto it = taskwk_st.begin(); it != taskwk_st.end();)
    {
        switch (it->second)
        {
            case TaskSt::Track:
                TrackTask(it->first);
                it++;
                break;
            case TaskSt::Untrack:
                UntrackTask(it->first);
                it = taskwk_st.erase(it);
                break;
            default:
                it++;
                break;
        }
    }
}

static TaskWk_State GetTaskState(EntityData1 *taskwk)
{
    TaskWk_State state;

    state.rotation = taskwk->Rotation;
    state.position = taskwk->Position;
    state.scale = taskwk->Scale;
    if (taskwk->CollisionInfo != NULL && taskwk->CollisionInfo->CollisionArray != NULL)
        state.col_center = taskwk->CollisionInfo->CollisionArray->center;

    return state;
}

static void TickTasks()
{
    // Check tasks
    CheckTasks();

    // Tick all tasks
    for (auto &i : taskwk_map)
    {
        i.second.bad--;
        if (i.second.bad > 0)
        {
            // Initialize task state
            i.second.state[1] = GetTaskState(i.first);
            i.second.state[0] = i.second.state[1];
        }
        else
        {
            // Push new task state
            i.second.state[0] = i.second.state[1];
            i.second.state[1] = GetTaskState(i.first);
        }
    }
}

static NJS_VECTOR InterpVector(NJS_VECTOR x, NJS_VECTOR y)
{
    // Return vector interpolated along the ticker
    return {
        (float)(x.x + (y.x - x.x) * ticker),
        (float)(x.y + (y.y - x.y) * ticker),
        (float)(x.z + (y.z - x.z) * ticker)
    };
}

static Rotation3 InterpRotation(Rotation3 x, Rotation3 y)
{
    return {
        (int)(x.x + (short)(y.x - x.x) * ticker),
        (int)(x.y + (short)(y.y - x.y) * ticker),
        (int)(x.z + (short)(y.z - x.z) * ticker)
    };
}

static void InterpTasks()
{
    // Check tasks
    CheckTasks();

    // Interpolate tasks along the ticker
    for (auto &i : taskwk_map)
    {
        if (i.second.bad <= 0)
        {
            i.first->Rotation = InterpRotation(i.second.state[0].rotation, i.second.state[1].rotation);
            i.first->Position = InterpVector(i.second.state[0].position, i.second.state[1].position);
            i.first->Scale = InterpVector(i.second.state[0].scale, i.second.state[1].scale);
            if (i.first->CollisionInfo != NULL && i.first->CollisionInfo->CollisionArray != NULL)
                i.first->CollisionInfo->CollisionArray->center = InterpVector(i.second.state[0].col_center, i.second.state[1].col_center);
        }
    }
}

static void RestoreTasks()
{
    // Check tasks
    CheckTasks();

    // Restore tasks to their intended state
    for (auto &i : taskwk_map)
    {
        if (i.second.bad <= 0)
        {
            i.first->Rotation = i.second.state[1].rotation;
            i.first->Position = i.second.state[1].position;
            i.first->Scale = i.second.state[1].scale;
            if (i.first->CollisionInfo != NULL && i.first->CollisionInfo->CollisionArray != NULL)
                i.first->CollisionInfo->CollisionArray->center = i.second.state[1].col_center;
        }
    }
}

// FrameLimit
static void __cdecl FrameLimit_r();
static Trampoline FrameLimit_t(0x007899E0, 0x007899E8, FrameLimit_r);

static void __cdecl FrameLimit_r()
{
	
}

// SetFrameMultiplier
static void __cdecl SetFrameMultiplier_r(int a1);
static Trampoline SetFrameMultiplier_t(0x007899A0, 0x007899A6, SetFrameMultiplier_r);

static void __cdecl SetFrameMultiplier_r(int a1)
{
    // Reset ticker
    ticker = 0.0;
    ticker_last = TickerClock::now();

	// Update game framerate
    *((int*)0x0389D7DC) = a1;
    game_framerate = GAME_FRAMERATE / a1;
}

// RunLogic
static void __cdecl RunLogic_r();
static Trampoline RunLogic_t(0x00413DD0, 0x00413EFD, RunLogic_r);

static void __cdecl RunLogic_r()
{
    // Push to button queue
    PushButtonQueue();

    // Increment ticker
    std::chrono::time_point<TickerClock> ticker_now = TickerClock::now();
    std::chrono::nanoseconds ticker_duration = ticker_now - ticker_last;
    ticker_last = ticker_now;

    ticker += ((long double)ticker_duration.count() / 1000000000.0) * game_framerate;
    if (ticker > 4.0)
        ticker = 0.0;

    if (ticker > 1.0)
    {
        // Tick logic without drawing
        MissedFrames = 0x40000000;

        while (ticker > 1.0)
        {
            ConsumeButtonQueue();
            RunSceneLogic();
            TickTasks();
            ticker -= 1.0;
        }
    }

    // Display logic
    InterpTasks();

    MissedFrames = 0;
    
    if (Camera_Data1 != NULL)
        Camera_Display_(Camera_Data1);
    DisplayAllObjects();
    Direct3D_DrawSpriteTable();

    RestoreTasks();

    // Restore framerate after in-game events
    if (FrameIncrement == 2)
        SetFrameRateMode(1, 1);
}

// LoadObject
static ObjectMaster *__cdecl LoadObject_r(LoadObj flags, int index, void(__cdecl *loadSub)(ObjectMaster *));
static Trampoline LoadObject_t(0x0040B860, 0x0040B935, LoadObject_r);

static ObjectMaster *__cdecl LoadObject_r(LoadObj flags, int index, void(__cdecl *loadSub)(ObjectMaster *))
{
    ObjectMaster *object;
    EntityData1 *data1;
    void *v6;
    EntityData2 *data2_ptr;
    void *v8;

    object = AllocateObjectMaster(index, loadSub);
    if (object != NULL)
    {
        if ((flags & LoadObj_Data1) != 0)
        {
            data1 = (EntityData1*)AllocateMemory(64);
            if (data1 == NULL)
            {
                DeleteObjectMaster(object);
                return NULL;
            }
            memset(data1, 0, sizeof(EntityData1));
            object->Data1 = data1;
            TrackTask(data1);
        }
        if ((flags & LoadObj_UnknownA) != 0)
        {
            v6 = (void*)AllocateMemory(56);
            if (v6 == NULL)
            {
                DeleteObjectMaster(object);
                return NULL;
            }
            memset(v6, 0, 56);
            object->UnknownA_ptr = v6;
        }

        if ((flags & LoadObj_Data2) != 0)
        {
            data2_ptr = (EntityData2*)AllocateMemory(64);
            if (data2_ptr == NULL)
            {
                DeleteObjectMaster(object);
                return NULL;
            }
            memset(data2_ptr, 0, sizeof(EntityData2));
            object->Data2 = data2_ptr;
        }

        if ((flags & LoadObj_UnknownB) == 0)
        {
            if (CurrentObject == NULL)
                object->field_30 = (int)*((void **)0x38F6E08);
            else
                object->field_30 = (int)CurrentObject;
        }
        else
        {
            v8 = (void*)AllocateMemory(16);
            if (v8 == NULL)
            {
                DeleteObjectMaster(object);
                return NULL;
            }
            memset(v8, 0, 16);
            object->UnknownB_ptr = v8;

            if (CurrentObject == NULL)
                object->field_30 = (int)*((void **)0x38F6E08);
            else
                object->field_30 = (int)CurrentObject;
        }
    }

    return object;
}

// DeleteObjectMaster
static void __cdecl DeleteObjectMaster_r(ObjectMaster *_this);
static Trampoline DeleteObjectMaster_t(0x0040B570, 0x0040B6BA, DeleteObjectMaster_r);

static void __cdecl DeleteObjectMaster_r(ObjectMaster *_this)
{
    ObjectFuncPtr deleteSub;
    _BOOL1 v2;
    ObjectMaster *previous;
    ObjectMaster *next;
    int i;
    SETDataUnion set;
    ObjectMaster *master;

    if (NaiZoGola(_this))
    {
        deleteSub = _this->DeleteSub;
        if (deleteSub)
        {
            deleteSub(_this);
        }
        v2 = _this->Child == 0;
        previous = _this->Previous;
        next = _this->Next;
        _this->MainSub = 0;
        _this->DisplaySub = 0;
        _this->DeleteSub = 0;
        if (!v2)
        {
            DeleteChildObjects(_this);
        }
        if (next)
        {
            if (previous)
            {
                next->Previous = previous;
                previous->Next = next;
                FREE_DATA:
                if (_this->Data1)
                {
                    UntrackTask(_this->Data1);
                    if (_this->Data1->CollisionInfo)
                    {
                        FreeEntityCollision(_this->Data1);
                    }
                    if (_this->Data1->field_3C)
                    {
                        FreeWhateverField3CIs(_this->Data1);
                    }
                    FreeMemory(_this->Data1);
                    _this->Data1 = 0;
                }

                if (_this->UnknownA_ptr)
                {
                    FreeMemory(_this->UnknownA_ptr);
                    _this->UnknownA_ptr = 0;
                }

                if (_this->Data2)
                {
                    FreeMemory(_this->Data2);
                    _this->Data2 = 0;
                }

                if (_this->UnknownB_ptr)
                {
                    FreeMemory(_this->UnknownB_ptr);
                    _this->UnknownB_ptr = 0;
                }

                set.SETData = (SETObjData *)_this->SETData.SETData;
                if (set.SETData)
                {
                    set.SETData->ObjInstance = 0;
                    _this->SETData.SETData->Flags &= ~1;
                    _this->SETData.SETData = 0;
                }

                master = MasterObjectArray;
                v2 = MasterObjectArray == 0;
                MasterObjectArray = _this;
                if (v2)
                {
                    _this->Next = 0;
                    _this->Previous = 0;
                    _this->Child = 0;
                    _this->Parent = 0;
                }
                else
                {
                    master->Previous = _this;
                    _this->Previous = 0;
                    _this->Child = 0;
                    _this->Parent = 0;
                    _this->Next = master;
                }
                return;
            }
        }
        else if (previous)
        {
            previous->Next = 0;
            goto FREE_DATA;
        }
        if (_this->Parent)
        {
            if (next)
            {
                next->Previous = 0;
            }
            _this->Parent->Child = next;
        }
        else
        {
            if (next)
            {
                next->Previous = 0;
            }
            i = 0;
            while (_this != ObjectListThing[i])
            {
                if (++i >= 8)
                {
                    goto FREE_DATA;
                }
            }
            ObjectListThing[i] = next;
        }
        goto FREE_DATA;
    }
}

// Direct3D_SetNJSTexture
static int __fastcall Direct3D_SetNJSTexture_r(NJS_TEXMEMLIST *t);
static FastcallFunctionHook<int, NJS_TEXMEMLIST*> Direct3D_SetNJSTexture_h((intptr_t)0x78CF20, Direct3D_SetNJSTexture_r);

static int __fastcall Direct3D_SetNJSTexture_r(NJS_TEXMEMLIST *t)
{
    if (t != NULL)
        return Direct3D_SetNJSTexture_h.Original(t);
    return 0;
}

// Mod information
extern "C"
{
    __declspec(dllexport) ModInfo SADXModInfo = { ModLoaderVer };

    __declspec(dllexport) void Init(const char *path, const HelperFunctions &)
    {

    }
}

// DLL main
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
