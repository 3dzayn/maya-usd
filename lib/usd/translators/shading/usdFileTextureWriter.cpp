//
// Copyright 2018 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include <maya/MFnDependencyNode.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

#include <pxr/pxr.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/types.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/output.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUtils/pipeline.h>

#include <mayaUsd/fileio/primWriterRegistry.h>
#include <mayaUsd/fileio/shaderWriter.h>
#include <mayaUsd/fileio/writeJobContext.h>
#include <mayaUsd/utils/util.h>

#include <boost/filesystem.hpp>

PXR_NAMESPACE_OPEN_SCOPE

class PxrUsdTranslators_FileTextureWriter : public UsdMayaShaderWriter
{
    public:
        PxrUsdTranslators_FileTextureWriter(
                const MFnDependencyNode& depNodeFn,
                const SdfPath& usdPath,
                UsdMayaWriteJobContext& jobCtx);

        void Write(const UsdTimeCode& usdTime) override;

        TfToken GetShadingAttributeNameForMayaAttrName(
                const TfToken& mayaAttrName) override;
};

PXRUSDMAYA_REGISTER_WRITER(file, PxrUsdTranslators_FileTextureWriter);

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,

    // Maya "file" node attribute names
    (alphaGain)
    (alphaOffset)
    (colorGain)
    (colorOffset)
    (defaultColor)
    (fileTextureName)
    (outAlpha)
    (outColor)
    (outColorR)
    (outColorG)
    (outColorB)
    (outTransparency)
    (outTransparencyR)
    (outTransparencyG)
    (outTransparencyB)
    (wrapU)
    (wrapV)

    // XXX: We duplicate these tokens here rather than create a dependency on
    // usdImaging in case the plugin is being built with imaging disabled.
    // If/when they move out of usdImaging to a place that is always available,
    // they should be pulled from there instead.
    (UsdUVTexture)
    (UsdPrimvarReader_float2)

    // UsdPrimvarReader_float2 Prim Name
    ((PrimvarReaderShaderName, "TexCoordReader"))

    // UsdPrimvarReader_float2 Input Names
    (varname)

    // UsdPrimvarReader_float2 Output Name
    (result)

    // UsdUVTexture Input Names
    (bias)
    (fallback)
    (file)
    (scale)
    (st)
    (wrapS)
    (wrapT)

    // Values for wrapS and wrapT
    (black)
    (repeat)

    // UsdUVTexture Output Names
    ((RGBOutputName, "rgb"))
    ((RedOutputName, "r"))
    ((GreenOutputName, "g"))
    ((BlueOutputName, "b"))
    ((AlphaOutputName, "a"))
);


PxrUsdTranslators_FileTextureWriter::PxrUsdTranslators_FileTextureWriter(
        const MFnDependencyNode& depNodeFn,
        const SdfPath& usdPath,
        UsdMayaWriteJobContext& jobCtx) :
    UsdMayaShaderWriter(depNodeFn, usdPath, jobCtx)
{
    // Create a UsdUVTexture shader as the "primary" shader for this writer.
    UsdShadeShader texShaderSchema =
        UsdShadeShader::Define(GetUsdStage(), GetUsdPath());
    if (!TF_VERIFY(
            texShaderSchema,
            "Could not define UsdShadeShader at path '%s'\n",
            GetUsdPath().GetText())) {
        return;
    }

    texShaderSchema.CreateIdAttr(VtValue(_tokens->UsdUVTexture));

    _usdPrim = texShaderSchema.GetPrim();
    if (!TF_VERIFY(
            _usdPrim,
            "Could not get UsdPrim for UsdShadeShader at path '%s'\n",
            texShaderSchema.GetPath().GetText())) {
        return;
    }

    // Now create a UsdPrimvarReader shader that the UsdUvTexture shader will
    // use.
    const SdfPath primvarReaderShaderPath =
        texShaderSchema.GetPath().AppendChild(_tokens->PrimvarReaderShaderName);
    UsdShadeShader primvarReaderShaderSchema =
        UsdShadeShader::Define(GetUsdStage(), primvarReaderShaderPath);

    primvarReaderShaderSchema.CreateIdAttr(
        VtValue(_tokens->UsdPrimvarReader_float2));

    // XXX: We'll eventually need to to determine which UV set to use if we're
    // not using the default (i.e. "map1" in Maya -> "st" in USD).
    primvarReaderShaderSchema.CreateInput(
        _tokens->varname,
        SdfValueTypeNames->Token).Set(UsdUtilsGetPrimaryUVSetName());

    UsdShadeOutput primvarReaderOutput =
        primvarReaderShaderSchema.CreateOutput(
            _tokens->result,
            SdfValueTypeNames->Float2);

    // Connect the output of the primvar reader to the texture coordinate
    // input of the UV texture.
    texShaderSchema.CreateInput(
        _tokens->st,
        SdfValueTypeNames->Float2).ConnectToSource(primvarReaderOutput);
}

/* virtual */
void
PxrUsdTranslators_FileTextureWriter::Write(const UsdTimeCode& usdTime)
{
    UsdMayaShaderWriter::Write(usdTime);

    MStatus status;

    const MFnDependencyNode depNodeFn(GetMayaObject(), &status);
    if (status != MS::kSuccess) {
        return;
    }

    UsdShadeShader shaderSchema(_usdPrim);
    if (!TF_VERIFY(
            shaderSchema,
            "Could not get UsdShadeShader schema for UsdPrim at path '%s'\n",
            _usdPrim.GetPath().GetText())) {
        return;
    }

    // File
    const MPlug fileTextureNamePlug =
        depNodeFn.findPlug(
            _tokens->fileTextureName.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    std::string fileTextureName(fileTextureNamePlug.asString(&status).asChar());
    if (status != MS::kSuccess) {
        return;
    }

    // WARNING: This extremely minimal attempt at making the file path relative
    //          to the USD stage is a stopgap measure intended to provide
    //          minimal interop. It will be replaced by proper use of Maya and
    //          USD asset resolvers.
    boost::filesystem::path usdDir(GetUsdStage()->GetRootLayer()->GetRealPath());
    usdDir = usdDir.parent_path();
    boost::system::error_code ec;
    boost::filesystem::path relativePath = boost::filesystem::relative(fileTextureName, usdDir, ec);
    if (!ec) {
        fileTextureName = relativePath.generic_string();
    }

    shaderSchema.CreateInput(
        _tokens->file,
        SdfValueTypeNames->Asset).Set(
            SdfAssetPath(fileTextureName.c_str()),
            usdTime);

    // The Maya file node's 'colorGain' and 'alphaGain' attributes map to the
    // UsdUVTexture's scale input.
    bool isScaleAuthored = false;
    GfVec4f scale(1.0f, 1.0f, 1.0f, 1.0f);

    // Color Gain
    const MPlug colorGainPlug =
        depNodeFn.findPlug(
            _tokens->colorGain.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(colorGainPlug)) {
        for (size_t i = 0u; i < GfVec3f::dimension; ++i) {
            scale[i] = colorGainPlug.child(i).asFloat(&status);
            if (status != MS::kSuccess) {
                return;
            }
        }

        isScaleAuthored = true;
    }

    // Alpha Gain
    const MPlug alphaGainPlug =
        depNodeFn.findPlug(
            _tokens->alphaGain.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(alphaGainPlug)) {
        scale[3u] = alphaGainPlug.asFloat(&status);
        if (status != MS::kSuccess) {
            return;
        }

        isScaleAuthored = true;
    }

    if (isScaleAuthored) {
        shaderSchema.CreateInput(
            _tokens->scale,
            SdfValueTypeNames->Float4).Set(scale, usdTime);
    }

    // The Maya file node's 'colorOffset' and 'alphaOffset' attributes map to
    // the UsdUVTexture's bias input.
    bool isBiasAuthored = false;
    GfVec4f bias(0.0f, 0.0f, 0.0f, 0.0f);

    // Color Offset
    const MPlug colorOffsetPlug =
        depNodeFn.findPlug(
            _tokens->colorOffset.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(colorOffsetPlug)) {
        for (size_t i = 0u; i < GfVec3f::dimension; ++i) {
            bias[i] = colorOffsetPlug.child(i).asFloat(&status);
            if (status != MS::kSuccess) {
                return;
            }
        }

        isBiasAuthored = true;
    }

    // Alpha Offset
    const MPlug alphaOffsetPlug =
        depNodeFn.findPlug(
            _tokens->alphaOffset.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(alphaOffsetPlug)) {
        bias[3u] = alphaOffsetPlug.asFloat(&status);
        if (status != MS::kSuccess) {
            return;
        }

        isBiasAuthored = true;
    }

    if (isBiasAuthored) {
        shaderSchema.CreateInput(
            _tokens->bias,
            SdfValueTypeNames->Float4).Set(bias, usdTime);
    }

    // Default Color
    const MPlug defaultColorPlug =
        depNodeFn.findPlug(
            _tokens->defaultColor.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    // The defaultColor plug does not include an alpha, so only look for
    // three components, even though we're putting the values in a GfVec4f.
    // We also don't check whether it is authored in Maya, since Maya's
    // unauthored value (0.5, 0.5, 0.5) differs from UsdUVTexture's fallback
    // value.
    GfVec4f fallback(0.0f, 0.0f, 0.0f, 1.0f);
    for (size_t i = 0u; i < GfVec3f::dimension; ++i) {
        fallback[i] = defaultColorPlug.child(i).asFloat(&status);
        if (status != MS::kSuccess) {
            return;
        }
    }

    shaderSchema.CreateInput(
        _tokens->fallback,
        SdfValueTypeNames->Float4).Set(fallback, usdTime);

    // Wrap U
    const MPlug wrapUPlug =
        depNodeFn.findPlug(
            _tokens->wrapU.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(wrapUPlug)) {
        const bool wrapU = wrapUPlug.asBool(&status);
        if (status != MS::kSuccess) {
            return;
        }

        const TfToken wrapS = wrapU ? _tokens->repeat : _tokens->black;
        shaderSchema.CreateInput(
            _tokens->wrapS,
            SdfValueTypeNames->Token).Set(wrapS, usdTime);
    }

    // Wrap V
    const MPlug wrapVPlug =
        depNodeFn.findPlug(
            _tokens->wrapV.GetText(),
            /* wantNetworkedPlug = */ true,
            &status);
    if (status != MS::kSuccess) {
        return;
    }

    if (UsdMayaUtil::IsAuthored(wrapVPlug)) {
        const bool wrapV = wrapVPlug.asBool(&status);
        if (status != MS::kSuccess) {
            return;
        }

        const TfToken wrapT = wrapV ? _tokens->repeat : _tokens->black;
        shaderSchema.CreateInput(
            _tokens->wrapT,
            SdfValueTypeNames->Token).Set(wrapT, usdTime);
    }
}

/* virtual */
TfToken
PxrUsdTranslators_FileTextureWriter::GetShadingAttributeNameForMayaAttrName(
        const TfToken& mayaAttrName)
{
    TfToken usdAttrName;
    SdfValueTypeName usdTypeName = SdfValueTypeNames->Float;

    if (mayaAttrName == _tokens->outColor) {
        usdAttrName = _tokens->RGBOutputName;
        usdTypeName = SdfValueTypeNames->Float3;
    } else if (mayaAttrName == _tokens->outColorR) {
        usdAttrName = _tokens->RedOutputName;
    } else if (mayaAttrName == _tokens->outColorG) {
        usdAttrName = _tokens->GreenOutputName;
    } else if (mayaAttrName == _tokens->outColorB) {
        usdAttrName = _tokens->BlueOutputName;
    } else if (mayaAttrName == _tokens->outAlpha ||
            mayaAttrName == _tokens->outTransparency ||
            mayaAttrName == _tokens->outTransparencyR ||
            mayaAttrName == _tokens->outTransparencyG ||
            mayaAttrName == _tokens->outTransparencyB) {
        usdAttrName = _tokens->AlphaOutputName;
    }

    if (!usdAttrName.IsEmpty()) {
        UsdShadeShader shaderSchema(_usdPrim);
        if (!shaderSchema) {
            return TfToken();
        }

        shaderSchema.CreateOutput(usdAttrName, usdTypeName);

        usdAttrName = UsdShadeUtils::GetFullName(usdAttrName, UsdShadeAttributeType::Output);
    }

    return usdAttrName;
}


PXR_NAMESPACE_CLOSE_SCOPE
