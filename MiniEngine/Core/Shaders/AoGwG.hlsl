
struct EntryRecord
{
    uint2 gridSize : SV_DispatchGrid;
};

struct AoPrepareDepthBuffers2_NodeInput
{
    uint2 DTid;
};

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
[NodeLocalRootArgumentsTableIndex(0)]
void AoPrepareDepthBuffers1_Node(
    DispatchNodeInputRecord<EntryRecord> inputData,
    [MaxRecords(4)] NodeOutput<AoPrepareDepthBuffers2_NodeInput> AoPrepareDepthBuffers2_Node,
    uint3 Gid  : SV_GroupID,
    uint  GI   : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 DTid : SV_DispatchThreadID)
{
    AoPrepareDepthBuffers1CS(Gid, GI, GTid, DTid);

    // Barrier(UAV_MEMORY, DEVICE_VISIBLE, 0);

    GroupNodeOutputRecords<AoPrepareDepthBuffers2_NodeInput> outRecs = AoPrepareDepthBuffers2_Node.GetGroupNodeOutputRecords(4);

    if (GI < 4)
        outRecs[GI].DTid = Gid.xy * 2 + uint2(GI & 1, GI >> 1);

    outRecs.OutputComplete();
}

#elif COMPILE_AOPREPAREDEPTHBUFFERS2CS

#define main AoPrepareDepthBuffers2CS
#include "AoPrepareDepthBuffers2CS.hlsl"
#undef main

[Shader("node")]
[NodeLaunch("thread")]
[NumThreads(8, 8, 1)]
[NodeLocalRootArgumentsTableIndex(1)]
void AoPrepareDepthBuffers2_Node(
    ThreadNodeInputRecord<AoPrepareDepthBuffers2_NodeInput> inputData,
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
