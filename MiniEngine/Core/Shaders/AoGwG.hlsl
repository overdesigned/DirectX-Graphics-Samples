
struct EntryRecord
{
    uint2 gridSize : SV_DispatchGrid;
    int   hierarchyDepth;
};

struct AoPrepareDepthBuffers2_NodeInput
{
    uint2 DTidTL;
    // TODO: Use node input to pass DS4x data
};

struct AoRender1_NodeInput
{
    uint2 DTid;
};

RWTexture2D<uint> PrepareDepthBuffers2HaloReadiness : register(u0, space1);

bool UpdateHaloReadiness(uint2 coord)
{
    uint readyCount = 0;
    InterlockedAdd(PrepareDepthBuffers2HaloReadiness[coord], 1, readyCount);
    return (readyCount & 0x3) == 0x3;
}

#define COMPILING_FOR_WORKGRAPH 1
#define GWG_GLOBALLYCOHERENT globallycoherent

#if COMPILE_AOPREPAREDEPTHBUFFERS1CS
#define main AoPrepareDepthBuffers1CS
#include "AoPrepareDepthBuffers1CS.hlsl"
#undef main

LocalRootSignature localRS = { SSAO_RootSig };

[Shader("node")]
[NodeLaunch("broadcasting")]
[NodeMaxDispatchGrid(480, 270, 1)]
[NumThreads(8, 8, 1)]
[WaveSize(64)]
[NodeLocalRootArgumentsTableIndex(0)]
void AoPrepareDepthBuffers1_Node(
    DispatchNodeInputRecord<EntryRecord> inputData,
    [MaxRecords(1)] NodeOutput<AoPrepareDepthBuffers2_NodeInput> AoPrepareDepthBuffers2_Node,
    uint3 Gid  : SV_GroupID,
    uint  GI   : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 DTid : SV_DispatchThreadID)
{
    AoPrepareDepthBuffers1CS(Gid, GI, GTid, DTid);

    if (inputData.Get().hierarchyDepth > 2)
    {
        GroupNodeOutputRecords<AoPrepareDepthBuffers2_NodeInput> outRecs = AoPrepareDepthBuffers2_Node.GetGroupNodeOutputRecords(1);

        outRecs[0].DTidTL = Gid.xy * 2;

        outRecs.OutputComplete();
    }
}

#elif COMPILE_AOPREPAREDEPTHBUFFERS2CS

#define main AoPrepareDepthBuffers2CS
#include "AoPrepareDepthBuffers2CS.hlsl"
#undef main

[Shader("node")]
[NodeLaunch("coalescing")]
[NumThreads(8, 8, 1)]
[WaveSize(64)]
[NodeLocalRootArgumentsTableIndex(1)]
void AoPrepareDepthBuffers2_Node(
    [MaxRecords(16)] GroupNodeInputRecords<AoPrepareDepthBuffers2_NodeInput> inputData,
    uint3 Gid  : SV_GroupID,
    uint  GI   : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 actualDTid : SV_DispatchThreadID)
{
    uint inputDataIndex = GI >> 2;

    if (inputDataIndex > inputData.Count())
        return;

    uint3 DTid = uint3(inputData.Get(inputDataIndex).DTidTL + uint2(GI & 0x1, (GI >> 1) & 0x1), 0);

    // No direct mapping between DTid and other indices.
    // This works only because AoPrepareDepthBuffers2CS only uses DTid and GI % 4.
    AoPrepareDepthBuffers2CS(Gid, GI, GTid, DTid);

    // DeviceMemoryBarrierWithGroupSync();
    // UpdateCompletionBitmap(Gid.xy);
}

#elif COMPILE_AORENDER1CS

#define main AoRender1CS
#include "AoRender1CS.hlsl"
#undef main

[Shader("node")]
[NodeLaunch("thread")]
[NumThreads(8, 8, 1)]
[NodeLocalRootArgumentsTableIndex(2)]
void AoRender1_Node(
    ThreadNodeInputRecord<AoRender1_NodeInput> inputData,
    uint3 unusedGid  : SV_GroupID,
    uint  unusedGI   : SV_GroupIndex,
    uint3 unusedGTid : SV_GroupThreadID,
    uint3 unusedDTid : SV_DispatchThreadID)
{
    uint3 DTid = uint3(inputData.Get().DTid, 0);
    uint3 Gid  = DTid >> 3;
    uint  GI   = DTid.x + DTid.y * 8;
    uint3 GTid = DTid & 7;

    AoPrepareDepthBuffers2CS(Gid, GI, GTid, DTid);
}

#endif
