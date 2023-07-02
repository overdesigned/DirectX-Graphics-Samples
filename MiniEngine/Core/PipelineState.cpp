//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard

#include "pch.h"
#include "GraphicsCore.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "Hash.h"
#include <map>
#include <thread>
#include <mutex>

using Math::IsAligned;
using namespace Graphics;
using Microsoft::WRL::ComPtr;
using namespace std;

static map< size_t, ComPtr<ID3D12PipelineState> > s_GraphicsPSOHashMap;
static map< size_t, ComPtr<ID3D12PipelineState> > s_ComputePSOHashMap;

void PSO::DestroyAll(void)
{
    s_GraphicsPSOHashMap.clear();
    s_ComputePSOHashMap.clear();
}


GraphicsPSO::GraphicsPSO(const wchar_t* Name)
    : PSO(Name)
{
    ZeroMemory(&m_PSODesc, sizeof(m_PSODesc));
    m_PSODesc.NodeMask = 1;
    m_PSODesc.SampleMask = 0xFFFFFFFFu;
    m_PSODesc.SampleDesc.Count = 1;
    m_PSODesc.InputLayout.NumElements = 0;
}

void GraphicsPSO::SetBlendState( const D3D12_BLEND_DESC& BlendDesc )
{
    m_PSODesc.BlendState = BlendDesc;
}

void GraphicsPSO::SetRasterizerState( const D3D12_RASTERIZER_DESC& RasterizerDesc )
{
    m_PSODesc.RasterizerState = RasterizerDesc;
}

void GraphicsPSO::SetDepthStencilState( const D3D12_DEPTH_STENCIL_DESC& DepthStencilDesc )
{
    m_PSODesc.DepthStencilState = DepthStencilDesc;
}

void GraphicsPSO::SetSampleMask( UINT SampleMask )
{
    m_PSODesc.SampleMask = SampleMask;
}

void GraphicsPSO::SetPrimitiveTopologyType( D3D12_PRIMITIVE_TOPOLOGY_TYPE TopologyType )
{
    ASSERT(TopologyType != D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED, "Can't draw with undefined topology");
    m_PSODesc.PrimitiveTopologyType = TopologyType;
}

void GraphicsPSO::SetPrimitiveRestart( D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBProps )
{
    m_PSODesc.IBStripCutValue = IBProps;
}

void GraphicsPSO::SetDepthTargetFormat(DXGI_FORMAT DSVFormat, UINT MsaaCount, UINT MsaaQuality )
{
    SetRenderTargetFormats(0, nullptr, DSVFormat, MsaaCount, MsaaQuality );
}

void GraphicsPSO::SetRenderTargetFormat( DXGI_FORMAT RTVFormat, DXGI_FORMAT DSVFormat, UINT MsaaCount, UINT MsaaQuality )
{
    SetRenderTargetFormats(1, &RTVFormat, DSVFormat, MsaaCount, MsaaQuality );
}

void GraphicsPSO::SetRenderTargetFormats( UINT NumRTVs, const DXGI_FORMAT* RTVFormats, DXGI_FORMAT DSVFormat, UINT MsaaCount, UINT MsaaQuality )
{
    ASSERT(NumRTVs == 0 || RTVFormats != nullptr, "Null format array conflicts with non-zero length");
    for (UINT i = 0; i < NumRTVs; ++i)
    {
        ASSERT(RTVFormats[i] != DXGI_FORMAT_UNKNOWN);
        m_PSODesc.RTVFormats[i] = RTVFormats[i];
    }
    for (UINT i = NumRTVs; i < m_PSODesc.NumRenderTargets; ++i)
        m_PSODesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    m_PSODesc.NumRenderTargets = NumRTVs;
    m_PSODesc.DSVFormat = DSVFormat;
    m_PSODesc.SampleDesc.Count = MsaaCount;
    m_PSODesc.SampleDesc.Quality = MsaaQuality;
}

void GraphicsPSO::SetInputLayout( UINT NumElements, const D3D12_INPUT_ELEMENT_DESC* pInputElementDescs )
{
    m_PSODesc.InputLayout.NumElements = NumElements;

    if (NumElements > 0)
    {
        D3D12_INPUT_ELEMENT_DESC* NewElements = (D3D12_INPUT_ELEMENT_DESC*)malloc(sizeof(D3D12_INPUT_ELEMENT_DESC) * NumElements);
        memcpy(NewElements, pInputElementDescs, NumElements * sizeof(D3D12_INPUT_ELEMENT_DESC));
        m_InputLayouts.reset((const D3D12_INPUT_ELEMENT_DESC*)NewElements);
    }
    else
        m_InputLayouts = nullptr;
}

void GraphicsPSO::Finalize()
{
    // Make sure the root signature is finalized first
    m_PSODesc.pRootSignature = m_RootSignature->GetSignature();
    ASSERT(m_PSODesc.pRootSignature != nullptr);

    m_PSODesc.InputLayout.pInputElementDescs = nullptr;
    size_t HashCode = Utility::HashState(&m_PSODesc);
    HashCode = Utility::HashState(m_InputLayouts.get(), m_PSODesc.InputLayout.NumElements, HashCode);
    m_PSODesc.InputLayout.pInputElementDescs = m_InputLayouts.get();

    ID3D12PipelineState** PSORef = nullptr;
    bool firstCompile = false;
    {
        static mutex s_HashMapMutex;
        lock_guard<mutex> CS(s_HashMapMutex);
        auto iter = s_GraphicsPSOHashMap.find(HashCode);

        // Reserve space so the next inquiry will find that someone got here first.
        if (iter == s_GraphicsPSOHashMap.end())
        {
            firstCompile = true;
            PSORef = s_GraphicsPSOHashMap[HashCode].GetAddressOf();
        }
        else
            PSORef = iter->second.GetAddressOf();
    }

    if (firstCompile)
    {
        ASSERT(m_PSODesc.DepthStencilState.DepthEnable != (m_PSODesc.DSVFormat == DXGI_FORMAT_UNKNOWN));
        ASSERT_SUCCEEDED( g_Device->CreateGraphicsPipelineState(&m_PSODesc, MY_IID_PPV_ARGS(&m_PSO)) );
        s_GraphicsPSOHashMap[HashCode].Attach(m_PSO);
        m_PSO->SetName(m_Name);
    }
    else
    {
        while (*PSORef == nullptr)
            this_thread::yield();
        m_PSO = *PSORef;
    }
}

void ComputePSO::Finalize()
{
    // Make sure the root signature is finalized first
    m_PSODesc.pRootSignature = m_RootSignature->GetSignature();
    ASSERT(m_PSODesc.pRootSignature != nullptr);

    size_t HashCode = Utility::HashState(&m_PSODesc);

    ID3D12PipelineState** PSORef = nullptr;
    bool firstCompile = false;
    {
        static mutex s_HashMapMutex;
        lock_guard<mutex> CS(s_HashMapMutex);
        auto iter = s_ComputePSOHashMap.find(HashCode);

        // Reserve space so the next inquiry will find that someone got here first.
        if (iter == s_ComputePSOHashMap.end())
        {
            firstCompile = true;
            PSORef = s_ComputePSOHashMap[HashCode].GetAddressOf();
        }
        else
            PSORef = iter->second.GetAddressOf();
    }

    if (firstCompile)
    {
        ASSERT_SUCCEEDED( g_Device->CreateComputePipelineState(&m_PSODesc, MY_IID_PPV_ARGS(&m_PSO)) );
        s_ComputePSOHashMap[HashCode].Attach(m_PSO);
        m_PSO->SetName(m_Name);
    }
    else
    {
        while (*PSORef == nullptr)
            this_thread::yield();
        m_PSO = *PSORef;
    }
}

ComputePSO::ComputePSO(const wchar_t* Name)
    : PSO(Name)
{
    ZeroMemory(&m_PSODesc, sizeof(m_PSODesc));
    m_PSODesc.NodeMask = 1;
}

GWGPso::GWGPso(const wchar_t* Name)
    : m_Name(Name)
{
}

void GWGPso::AddLocalRootSignature(const std::wstring& LibName, const std::wstring& RootSignatureName)
{
    ASSERT(m_LocalRootSignatures.find(RootSignatureName) == m_LocalRootSignatures.end());

    ComPtr<ID3D12DeviceExperimental> spDevice;
    ComPtr<ID3D12Device>(g_Device).As(&spDevice);

    auto& libCode = m_Libraries[LibName];

    Microsoft::WRL::ComPtr<ID3D12RootSignature> pRootSig;

    ASSERT_SUCCEEDED(spDevice->CreateRootSignatureFromSubobjectInLibrary(
        0, libCode.pShaderBytecode, libCode.BytecodeLength, RootSignatureName.c_str(), IID_PPV_ARGS(&pRootSig)));

    m_LocalRootSignatures[RootSignatureName] = pRootSig;
}

void GWGPso::Finalize(const wchar_t* workGraphName)
{
    ComPtr<ID3D12DeviceExperimental> spDevice;
    ComPtr<ID3D12Device>(g_Device).As(&spDevice);

    CD3DX12_STATE_OBJECT_DESC SO(D3D12_STATE_OBJECT_TYPE_EXECUTABLE);

    for (auto& libCode : m_Libraries)
    {
        auto pLib = SO.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
        pLib->SetDXILLibrary(&libCode.second);
    }

    auto pWG = SO.CreateSubobject<CD3DX12_WORK_GRAPH_SUBOBJECT>();
    pWG->IncludeAllAvailableNodes(); // Auto populate the graph
    pWG->SetProgramName(workGraphName);

    std::map<std::wstring, const CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT*> localRootSigSubObjs;

    for (auto& localRsIter : m_LocalRootSignatures)
    {
        auto pLocalRootSignature = SO.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
        pLocalRootSignature->SetRootSignature(localRsIter.second.Get());

        localRootSigSubObjs[localRsIter.first] = pLocalRootSignature;
    }

    for (auto& nodeToLocalRSAssociation : m_NodeToLocalRootSignatureAssociations)
    {
        auto associationSubObj = SO.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
        associationSubObj->AddExport(nodeToLocalRSAssociation.first.c_str());
        associationSubObj->SetSubobjectToAssociate(*localRootSigSubObjs[nodeToLocalRSAssociation.second]);
    }

    ASSERT_SUCCEEDED(spDevice->CreateStateObject(SO, IID_PPV_ARGS(&m_StateObject)));

    ComPtr<ID3D12StateObjectProperties1> spSOProps;
    m_StateObject.As(&spSOProps);
    m_hWorkGraph = spSOProps->GetProgramIdentifier(workGraphName);

    ComPtr<ID3D12WorkGraphProperties> spWGProps;
    m_StateObject.As(&spWGProps);
    UINT WorkGraphIndex = spWGProps->GetWorkGraphIndex(workGraphName);

    spWGProps->GetWorkGraphMemoryRequirements(WorkGraphIndex, &m_MemReqs);

    CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(m_MemReqs.MaxSizeInBytes);
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    CD3DX12_HEAP_PROPERTIES hp(D3D12_HEAP_TYPE_DEFAULT);

    ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(
        &hp,
        D3D12_HEAP_FLAG_NONE,
        &rd,
        D3D12_RESOURCE_STATE_COMMON,
        NULL,
        IID_PPV_ARGS(&m_BackingResource)));

    m_BackingMemory.SizeInBytes = m_MemReqs.MaxSizeInBytes;
    m_BackingMemory.StartAddress = m_BackingResource->GetGPUVirtualAddress();
}
