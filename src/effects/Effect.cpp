/**********************************************************************

  Audacity: A Digital Audio Editor

  Effect.cpp

  Dominic Mazzoni
  Vaughan Johnson
  Martyn Shaw

*******************************************************************//**

\class Effect
\brief Base class for many of the effects in Audacity.

*//****************************************************************//**

\class EffectDialog
\brief New (Jun-2006) base class for effects dialogs.  Likely to get
greater use in future.

*//*******************************************************************/

#include "../Audacity.h"
#include "Effect.h"

#include "../Experimental.h"

#include <algorithm>

#include <wx/choice.h>
#include <wx/defs.h>
#include <wx/listbox.h>
#include <wx/sizer.h>

#include "../AudioIO.h"
#include "../LabelTrack.h"
#include "../Mix.h"
#include "../PluginManager.h"
#include "../Project.h"
#include "../ProjectAudioManager.h"
#include "../ProjectSettings.h"
#include "../ShuttleGui.h"
#include "../Shuttle.h"
#include "../ViewInfo.h"
#include "../WaveTrack.h"
#include "../widgets/ProgressDialog.h"
#include "../ondemand/ODManager.h"
#include "../tracks/playabletrack/wavetrack/ui/WaveTrackView.h"
#include "../tracks/playabletrack/wavetrack/ui/WaveTrackViewConstants.h"
#include "../widgets/NumericTextCtrl.h"
#include "../widgets/AudacityMessageBox.h"
#include "../widgets/ErrorDialog.h"

#include <unordered_map>

// Effect application counter
int Effect::nEffectsDone=0;

static const int kPlayID = 20102;
static const int kRewindID = 20103;
static const int kFFwdID = 20104;

const wxString Effect::kUserPresetIdent = wxT("User Preset:");
const wxString Effect::kFactoryPresetIdent = wxT("Factory Preset:");
const wxString Effect::kCurrentSettingsIdent = wxT("<Current Settings>");
const wxString Effect::kFactoryDefaultsIdent = wxT("<Factory Defaults>");

using t2bHash = std::unordered_map< void*, bool >;

namespace {

Effect::VetoDialogHook &GetVetoDialogHook()
{
   static Effect::VetoDialogHook sHook = nullptr;
   return sHook;
}

}

auto Effect::SetVetoDialogHook( VetoDialogHook hook )
   -> VetoDialogHook
{
   auto &theHook = GetVetoDialogHook();
   auto result = theHook;
   theHook = hook;
   return result;
}

Effect::Effect()
{
   mClient = NULL;

   mTracks = NULL;
   mT0 = 0.0;
   mT1 = 0.0;
   mDuration = 0.0;
   mIsPreview = false;
   mIsLinearEffect = false;
   mPreviewWithNotSelected = false;
   mPreviewFullSelection = false;
   mNumTracks = 0;
   mNumGroups = 0;
   mProgress = NULL;

   mUIParent = NULL;
   mUIDialog = NULL;

   mNumAudioIn = 0;
   mNumAudioOut = 0;

   mBufferSize = 0;
   mBlockSize = 0;
   mNumChannels = 0;

   mUIDebug = false;

   // PRL:  I think this initialization of mProjectRate doesn't matter
   // because it is always reassigned in DoEffect before it is used
   gPrefs->Read(wxT("/SamplingRate/DefaultProjectSampleRate"),
                &mProjectRate,
                AudioIOBase::GetOptimalSupportedSampleRate());

   mIsBatch = false;
}

Effect::~Effect()
{
   if (mUIDialog)
   {
      mUIDialog->Close();
   }
}

// EffectDefinitionInterface implementation

EffectType Effect::GetType()
{
   if (mClient)
   {
      return mClient->GetType();
   }

   return EffectTypeNone;
}

PluginPath Effect::GetPath()
{
   if (mClient)
   {
      return mClient->GetPath();
   }

   return BUILTIN_EFFECT_PREFIX + GetSymbol().Internal();
}

ComponentInterfaceSymbol Effect::GetSymbol()
{
   if (mClient)
   {
      return mClient->GetSymbol();
   }

   return {};
}

VendorSymbol Effect::GetVendor()
{
   if (mClient)
   {
      return mClient->GetVendor();
   }

   return XO("Audacity");
}

wxString Effect::GetVersion()
{
   if (mClient)
   {
      return mClient->GetVersion();
   }

   return AUDACITY_VERSION_STRING;
}

TranslatableString Effect::GetDescription()
{
   if (mClient)
   {
      return mClient->GetDescription();
   }

   return {};
}

EffectFamilySymbol Effect::GetFamily()
{
   if (mClient)
   {
      return mClient->GetFamily();
   }

   // Unusually, the internal and visible strings differ for the built-in
   // effect family.
   return { wxT("Audacity"), XO("Built-in") };
}

bool Effect::IsInteractive()
{
   if (mClient)
   {
      return mClient->IsInteractive();
   }

   return true;
}

bool Effect::IsDefault()
{
   if (mClient)
   {
      return mClient->IsDefault();
   }

   return true;
}

bool Effect::IsLegacy()
{
   if (mClient)
   {
      return false;
   }

   return true;
}

bool Effect::SupportsRealtime()
{
   if (mClient)
   {
      return mClient->SupportsRealtime();
   }

   return false;
}

bool Effect::SupportsAutomation()
{
   if (mClient)
   {
      return mClient->SupportsAutomation();
   }

   return true;
}

// EffectClientInterface implementation

bool Effect::SetHost(EffectHostInterface *host)
{
   if (mClient)
   {
      return mClient->SetHost(host);
   }

   return true;
}

unsigned Effect::GetAudioInCount()
{
   if (mClient)
   {
      return mClient->GetAudioInCount();
   }

   return 0;
}

unsigned Effect::GetAudioOutCount()
{
   if (mClient)
   {
      return mClient->GetAudioOutCount();
   }

   return 0;
}

int Effect::GetMidiInCount()
{
   if (mClient)
   {
      return mClient->GetMidiInCount();
   }

   return 0;
}

int Effect::GetMidiOutCount()
{
   if (mClient)
   {
      return mClient->GetMidiOutCount();
   }

   return 0;
}

void Effect::SetSampleRate(double rate)
{
   if (mClient)
   {
      mClient->SetSampleRate(rate);
   }

   mSampleRate = rate;
}

size_t Effect::SetBlockSize(size_t maxBlockSize)
{
   if (mClient)
   {
      return mClient->SetBlockSize(maxBlockSize);
   }

   mBlockSize = maxBlockSize;

   return mBlockSize;
}

size_t Effect::GetBlockSize() const
{
   if (mClient)
   {
      return mClient->GetBlockSize();
   }

   return mBlockSize;
}

sampleCount Effect::GetLatency()
{
   if (mClient)
   {
      return mClient->GetLatency();
   }

   return 0;
}

size_t Effect::GetTailSize()
{
   if (mClient)
   {
      return mClient->GetTailSize();
   }

   return 0;
}

bool Effect::IsReady()
{
   if (mClient)
   {
      return mClient->IsReady();
   }

   return true;
}

bool Effect::ProcessInitialize(sampleCount totalLen, ChannelNames chanMap)
{
   if (mClient)
   {
      return mClient->ProcessInitialize(totalLen, chanMap);
   }

   return true;
}

bool Effect::ProcessFinalize()
{
   if (mClient)
   {
      return mClient->ProcessFinalize();
   }

   return true;
}

size_t Effect::ProcessBlock(float **inBlock, float **outBlock, size_t blockLen)
{
   if (mClient)
   {
      return mClient->ProcessBlock(inBlock, outBlock, blockLen);
   }

   return 0;
}

bool Effect::RealtimeInitialize()
{
   if (mClient)
   {
      mBlockSize = mClient->SetBlockSize(512);
      return mClient->RealtimeInitialize();
   }

   mBlockSize = 512;

   return false;
}

bool Effect::RealtimeAddProcessor(unsigned numChannels, float sampleRate)
{
   if (mClient)
   {
      return mClient->RealtimeAddProcessor(numChannels, sampleRate);
   }

   return true;
}

bool Effect::RealtimeFinalize()
{
   if (mClient)
   {
      return mClient->RealtimeFinalize();
   }

   return false;
}

bool Effect::RealtimeSuspend()
{
   if (mClient)
      return mClient->RealtimeSuspend();

   return true;
}

bool Effect::RealtimeResume()
{
   if (mClient)
      return mClient->RealtimeResume();

   return true;
}

bool Effect::RealtimeProcessStart()
{
   if (mClient)
   {
      return mClient->RealtimeProcessStart();
   }

   return true;
}

size_t Effect::RealtimeProcess(int group,
                                    float **inbuf,
                                    float **outbuf,
                                    size_t numSamples)
{
   if (mClient)
   {
      return mClient->RealtimeProcess(group, inbuf, outbuf, numSamples);
   }

   return 0;
}

bool Effect::RealtimeProcessEnd()
{
   if (mClient)
   {
      return mClient->RealtimeProcessEnd();
   }

   return true;
}

bool Effect::ShowInterface(wxWindow &parent,
   const EffectDialogFactory &factory, bool forceModal)
{
   if (!IsInteractive())
   {
      return true;
   }

   if (mUIDialog)
   {
      if ( mUIDialog->Close(true) )
         mUIDialog = nullptr;
      return false;
   }

   if (mClient)
   {
      return mClient->ShowInterface(parent, factory, forceModal);
   }

   // mUIDialog is null
   auto cleanup = valueRestorer( mUIDialog );
   
   if ( factory )
      mUIDialog = factory(parent, this, this);
   if (!mUIDialog)
   {
      return false;
   }


   mUIDialog->Layout();
   mUIDialog->Fit();
   mUIDialog->SetMinSize(mUIDialog->GetSize());

   auto hook = GetVetoDialogHook();
   if( hook && hook( mUIDialog ) )
      return false;

   if( SupportsRealtime() && !forceModal )
   {
      mUIDialog->Show();
      cleanup.release();

      // Return false to bypass effect processing
      return false;
   }

   bool res = mUIDialog->ShowModal() != 0;

   return res;
}

bool Effect::GetAutomationParameters(CommandParameters & parms)
{
   if (mClient)
   {
      return mClient->GetAutomationParameters(parms);
   }

   return true;
}

bool Effect::SetAutomationParameters(CommandParameters & parms)
{
   if (mClient)
   {
      return mClient->SetAutomationParameters(parms);
   }

   return true;
}

bool Effect::LoadUserPreset(const RegistryPath & name)
{
   if (mClient)
   {
      return mClient->LoadUserPreset(name);
   }

   wxString parms;
   if (!GetPrivateConfig(name, wxT("Parameters"), parms))
   {
      return false;
   }

   return SetAutomationParameters(parms);
}

bool Effect::SaveUserPreset(const RegistryPath & name)
{
   if (mClient)
   {
      return mClient->SaveUserPreset(name);
   }

   wxString parms;
   if (!GetAutomationParameters(parms))
   {
      return false;
   }

   return SetPrivateConfig(name, wxT("Parameters"), parms);
}

RegistryPaths Effect::GetFactoryPresets()
{
   if (mClient)
   {
      return mClient->GetFactoryPresets();
   }

   return {};
}

bool Effect::LoadFactoryPreset(int id)
{
   if (mClient)
   {
      return mClient->LoadFactoryPreset(id);
   }

   return true;
}

bool Effect::LoadFactoryDefaults()
{
   if (mClient)
   {
      return mClient->LoadFactoryDefaults();
   }

   return LoadUserPreset(GetFactoryDefaultsGroup());
}

// EffectUIClientInterface implementation

void Effect::SetHostUI(EffectUIHostInterface *WXUNUSED(host))
{
}

bool Effect::PopulateUI(wxWindow *parent)
{
   mUIParent = parent;
   mUIParent->PushEventHandler(this);

//   LoadUserPreset(GetCurrentSettingsGroup());

   ShuttleGui S(mUIParent, eIsCreating);
   PopulateOrExchange(S);

   mUIParent->SetMinSize(mUIParent->GetSizer()->GetMinSize());

   return true;
}

bool Effect::IsGraphicalUI()
{
   return false;
}

bool Effect::ValidateUI()
{
   return mUIParent->Validate();
}

bool Effect::HideUI()
{
   return true;
}

bool Effect::CloseUI()
{
   if (mUIParent)
      mUIParent->RemoveEventHandler(this);

   mUIParent = NULL;
   mUIDialog = NULL;

   return true;
}

bool Effect::CanExportPresets()
{
   return false;
}

void Effect::ExportPresets()
{
}

void Effect::ImportPresets()
{
}

bool Effect::HasOptions()
{
   return false;
}

void Effect::ShowOptions()
{
}

// EffectHostInterface implementation

double Effect::GetDefaultDuration()
{
   return 30.0;
}

double Effect::GetDuration()
{
   if (mDuration < 0.0)
   {
      mDuration = 0.0;
   }

   return mDuration;
}

NumericFormatSymbol Effect::GetDurationFormat()
{
   return mDurationFormat;
}

NumericFormatSymbol Effect::GetSelectionFormat()
{
   return ProjectSettings::Get( *FindProject() ).GetSelectionFormat();
}

void Effect::SetDuration(double seconds)
{
   if (seconds < 0.0)
   {
      seconds = 0.0;
   }

   if (GetType() == EffectTypeGenerate)
   {
      SetPrivateConfig(GetCurrentSettingsGroup(), wxT("LastUsedDuration"), seconds);
   }

   mDuration = seconds;

   mIsSelection = false;

   return;
}

RegistryPath Effect::GetUserPresetsGroup(const RegistryPath & name)
{
   RegistryPath group = wxT("UserPresets");
   if (!name.empty())
   {
      group += wxCONFIG_PATH_SEPARATOR + name;
   }

   return group;
}

RegistryPath Effect::GetCurrentSettingsGroup()
{
   return wxT("CurrentSettings");
}

RegistryPath Effect::GetFactoryDefaultsGroup()
{
   return wxT("FactoryDefaults");
}

wxString Effect::GetSavedStateGroup()
{
   return wxT("SavedState");
}

// ConfigClientInterface implementation
bool Effect::HasSharedConfigGroup(const RegistryPath & group)
{
   return PluginManager::Get().HasSharedConfigGroup(GetID(), group);
}

bool Effect::GetSharedConfigSubgroups(const RegistryPath & group, RegistryPaths &subgroups)
{
   return PluginManager::Get().GetSharedConfigSubgroups(GetID(), group, subgroups);
}

bool Effect::GetSharedConfig(const RegistryPath & group, const RegistryPath & key, wxString & value, const wxString & defval)
{
   return PluginManager::Get().GetSharedConfig(GetID(), group, key, value, defval);
}

bool Effect::GetSharedConfig(const RegistryPath & group, const RegistryPath & key, int & value, int defval)
{
   return PluginManager::Get().GetSharedConfig(GetID(), group, key, value, defval);
}

bool Effect::GetSharedConfig(const RegistryPath & group, const RegistryPath & key, bool & value, bool defval)
{
   return PluginManager::Get().GetSharedConfig(GetID(), group, key, value, defval);
}

bool Effect::GetSharedConfig(const RegistryPath & group, const RegistryPath & key, float & value, float defval)
{
   return PluginManager::Get().GetSharedConfig(GetID(), group, key, value, defval);
}

bool Effect::GetSharedConfig(const RegistryPath & group, const RegistryPath & key, double & value, double defval)
{
   return PluginManager::Get().GetSharedConfig(GetID(), group, key, value, defval);
}

bool Effect::SetSharedConfig(const RegistryPath & group, const RegistryPath & key, const wxString & value)
{
   return PluginManager::Get().SetSharedConfig(GetID(), group, key, value);
}

bool Effect::SetSharedConfig(const RegistryPath & group, const RegistryPath & key, const int & value)
{
   return PluginManager::Get().SetSharedConfig(GetID(), group, key, value);
}

bool Effect::SetSharedConfig(const RegistryPath & group, const RegistryPath & key, const bool & value)
{
   return PluginManager::Get().SetSharedConfig(GetID(), group, key, value);
}

bool Effect::SetSharedConfig(const RegistryPath & group, const RegistryPath & key, const float & value)
{
   return PluginManager::Get().SetSharedConfig(GetID(), group, key, value);
}

bool Effect::SetSharedConfig(const RegistryPath & group, const RegistryPath & key, const double & value)
{
   return PluginManager::Get().SetSharedConfig(GetID(), group, key, value);
}

bool Effect::RemoveSharedConfigSubgroup(const RegistryPath & group)
{
   return PluginManager::Get().RemoveSharedConfigSubgroup(GetID(), group);
}

bool Effect::RemoveSharedConfig(const RegistryPath & group, const RegistryPath & key)
{
   return PluginManager::Get().RemoveSharedConfig(GetID(), group, key);
}

bool Effect::HasPrivateConfigGroup(const RegistryPath & group)
{
   return PluginManager::Get().HasPrivateConfigGroup(GetID(), group);
}

bool Effect::GetPrivateConfigSubgroups(const RegistryPath & group, RegistryPaths & subgroups)
{
   return PluginManager::Get().GetPrivateConfigSubgroups(GetID(), group, subgroups);
}

bool Effect::GetPrivateConfig(const RegistryPath & group, const RegistryPath & key, wxString & value, const wxString & defval)
{
   return PluginManager::Get().GetPrivateConfig(GetID(), group, key, value, defval);
}

bool Effect::GetPrivateConfig(const RegistryPath & group, const RegistryPath & key, int & value, int defval)
{
   return PluginManager::Get().GetPrivateConfig(GetID(), group, key, value, defval);
}

bool Effect::GetPrivateConfig(const RegistryPath & group, const RegistryPath & key, bool & value, bool defval)
{
   return PluginManager::Get().GetPrivateConfig(GetID(), group, key, value, defval);
}

bool Effect::GetPrivateConfig(const RegistryPath & group, const RegistryPath & key, float & value, float defval)
{
   return PluginManager::Get().GetPrivateConfig(GetID(), group, key, value, defval);
}

bool Effect::GetPrivateConfig(const RegistryPath & group, const RegistryPath & key, double & value, double defval)
{
   return PluginManager::Get().GetPrivateConfig(GetID(), group, key, value, defval);
}

bool Effect::SetPrivateConfig(const RegistryPath & group, const RegistryPath & key, const wxString & value)
{
   return PluginManager::Get().SetPrivateConfig(GetID(), group, key, value);
}

bool Effect::SetPrivateConfig(const RegistryPath & group, const RegistryPath & key, const int & value)
{
   return PluginManager::Get().SetPrivateConfig(GetID(), group, key, value);
}

bool Effect::SetPrivateConfig(const RegistryPath & group, const RegistryPath & key, const bool & value)
{
   return PluginManager::Get().SetPrivateConfig(GetID(), group, key, value);
}

bool Effect::SetPrivateConfig(const RegistryPath & group, const RegistryPath & key, const float & value)
{
   return PluginManager::Get().SetPrivateConfig(GetID(), group, key, value);
}

bool Effect::SetPrivateConfig(const RegistryPath & group, const RegistryPath & key, const double & value)
{
   return PluginManager::Get().SetPrivateConfig(GetID(), group, key, value);
}

bool Effect::RemovePrivateConfigSubgroup(const RegistryPath & group)
{
   return PluginManager::Get().RemovePrivateConfigSubgroup(GetID(), group);
}

bool Effect::RemovePrivateConfig(const RegistryPath & group, const RegistryPath & key)
{
   return PluginManager::Get().RemovePrivateConfig(GetID(), group, key);
}

// Effect implementation

PluginID Effect::GetID()
{
   if (mClient)
   {
      return PluginManager::GetID(mClient);
   }

   return PluginManager::GetID(this);
}

bool Effect::Startup(EffectClientInterface *client)
{
   // Let destructor know we need to be shutdown
   mClient = client;

   // Set host so client startup can use our services
   if (!SetHost(this))
   {
      // Bail if the client startup fails
      mClient = NULL;
      return false;
   }

   mNumAudioIn = GetAudioInCount();
   mNumAudioOut = GetAudioOutCount();

   bool haveDefaults;
   GetPrivateConfig(GetFactoryDefaultsGroup(), wxT("Initialized"), haveDefaults, false);
   if (!haveDefaults)
   {
      SaveUserPreset(GetFactoryDefaultsGroup());
      SetPrivateConfig(GetFactoryDefaultsGroup(), wxT("Initialized"), true);
   }
   LoadUserPreset(GetCurrentSettingsGroup());

   return Startup();
}

bool Effect::Startup()
{
   return true;
}

bool Effect::GetAutomationParameters(wxString & parms)
{
   CommandParameters eap;

   if (mUIDialog && !TransferDataFromWindow())
   {
      return false;
   }

   ShuttleGetAutomation S;
   S.mpEap = &eap;
   if( DefineParams( S ) ){
      ;// got eap value using DefineParams.
   }
   // Won't be needed in future
   else if (!GetAutomationParameters(eap))
   {
      return false;
   }

   return eap.GetParameters(parms);
}

bool Effect::SetAutomationParameters(const wxString & parms)
{
   wxString preset = parms;
   bool success = false;
   if (preset.StartsWith(kUserPresetIdent))
   {
      preset.Replace(kUserPresetIdent, wxEmptyString, false);
      success = LoadUserPreset(GetUserPresetsGroup(preset));
   }
   else if (preset.StartsWith(kFactoryPresetIdent))
   {
      preset.Replace(kFactoryPresetIdent, wxEmptyString, false);
      auto presets = GetFactoryPresets();
      success = LoadFactoryPreset( make_iterator_range( presets ).index( preset ) );
   }
   else if (preset.StartsWith(kCurrentSettingsIdent))
   {
      preset.Replace(kCurrentSettingsIdent, wxEmptyString, false);
      success = LoadUserPreset(GetCurrentSettingsGroup());
   }
   else if (preset.StartsWith(kFactoryDefaultsIdent))
   {
      preset.Replace(kFactoryDefaultsIdent, wxEmptyString, false);
      success = LoadUserPreset(GetFactoryDefaultsGroup());
   }
   else
   {
      CommandParameters eap(parms);
      ShuttleSetAutomation S;
      S.SetForValidating( &eap );
      // DefineParams returns false if not defined for this effect.
      if( !DefineParams( S ) )
         // the old method...
         success = SetAutomationParameters(eap);
      else if( !S.bOK )
         success = false;
      else{
         success = true;
         S.SetForWriting( &eap );
         DefineParams( S );
      }
   }

   if (!success)
   {
      Effect::MessageBox(
         XO("%s: Could not load settings below. Default settings will be used.\n\n%s")
            .Format( GetName(), preset ) );
      // We are using defualt settings and we still wish to continue.
      return true;
      //return false;
   }

   if (!mUIDialog)
   {
      return true;
   }

   return TransferDataToWindow();
}

RegistryPaths Effect::GetUserPresets()
{
   RegistryPaths presets;

   GetPrivateConfigSubgroups(GetUserPresetsGroup(wxEmptyString), presets);

   std::sort( presets.begin(), presets.end() );

   return presets;
}

bool Effect::HasCurrentSettings()
{
   return HasPrivateConfigGroup(GetCurrentSettingsGroup());
}

bool Effect::HasFactoryDefaults()
{
   return HasPrivateConfigGroup(GetFactoryDefaultsGroup());
}

wxString Effect::GetPreset(wxWindow * parent, const wxString & parms)
{
   EffectPresetsDialog dlg(parent, this);
   dlg.Layout();
   dlg.Fit();
   dlg.SetSize(dlg.GetMinSize());
   dlg.CenterOnParent();
   dlg.SetSelected(parms);

   if (dlg.ShowModal())
   {
      return dlg.GetSelected();
   }

   return wxEmptyString;
}

wxString Effect::ManualPage()
{
   return wxEmptyString;
}

wxString Effect::HelpPage()
{
   return wxEmptyString;
}

bool Effect::IsBatchProcessing()
{
   return mIsBatch;
}

void Effect::SetBatchProcessing(bool start)
{
   mIsBatch = start;

   if (start)
   {
      SaveUserPreset(GetSavedStateGroup());
   }
   else
   {
      LoadUserPreset(GetSavedStateGroup());
   }
}

bool Effect::DoEffect(wxWindow &parent,
                      double projectRate,
                      TrackList *list,
                      TrackFactory *factory,
                      NotifyingSelectedRegion &selectedRegion,
                      const EffectDialogFactory &dialogFactory)
{
   wxASSERT(selectedRegion.duration() >= 0.0);

   mOutputTracks.reset();

   mpSelectedRegion = &selectedRegion;
   mFactory = factory;
   mProjectRate = projectRate;
   mTracks = list;

   // Update track/group counts
   CountWaveTracks();

   bool isSelection = false;

   mDuration = 0.0;
   if (GetType() == EffectTypeGenerate)
   {
      GetPrivateConfig(GetCurrentSettingsGroup(), wxT("LastUsedDuration"), mDuration, GetDefaultDuration());
   }

   WaveTrack *newTrack{};
   bool success = false;
   auto oldDuration = mDuration;

   auto cleanup = finally( [&] {
      if (!success) {
         if (newTrack) {
            mTracks->Remove(newTrack);
         }
         // LastUsedDuration may have been modified by Preview.
         SetDuration(oldDuration);
      }

      End();
      ReplaceProcessedTracks( false );
   } );

   // We don't yet know the effect type for code in the Nyquist Prompt, so
   // assume it requires a track and handle errors when the effect runs.
   if ((GetType() == EffectTypeGenerate || GetPath() == NYQUIST_PROMPT_ID) && (mNumTracks == 0)) {
      newTrack = mTracks->Add(mFactory->NewWaveTrack());
      newTrack->SetSelected(true);
   }

   mT0 = selectedRegion.t0();
   mT1 = selectedRegion.t1();
   if (mT1 > mT0)
   {
      // there is a selection: let's fit in there...
      // MJS: note that this is just for the TTC and is independent of the track rate
      // but we do need to make sure we have the right number of samples at the project rate
      double quantMT0 = QUANTIZED_TIME(mT0, mProjectRate);
      double quantMT1 = QUANTIZED_TIME(mT1, mProjectRate);
      mDuration = quantMT1 - quantMT0;
      isSelection = true;
      mT1 = mT0 + mDuration;
   }

   mDurationFormat = isSelection
      ? NumericConverter::TimeAndSampleFormat()
      : NumericConverter::DefaultSelectionFormat();

#ifdef EXPERIMENTAL_SPECTRAL_EDITING
   mF0 = selectedRegion.f0();
   mF1 = selectedRegion.f1();
   wxArrayString Names;
   if( mF0 != SelectedRegion::UndefinedFrequency )
      Names.push_back(wxT("control-f0"));
   if( mF1 != SelectedRegion::UndefinedFrequency )
      Names.push_back(wxT("control-f1"));
   SetPresetParameters( &Names, NULL );

#endif
   CountWaveTracks();

   // Note: Init may read parameters from preferences
   if (!Init())
   {
      return false;
   }

   // Prompting will be bypassed when applying an effect that has already
   // been configured, e.g. repeating the last effect on a different selection.
   // Prompting may call Effect::Preview
   if ( dialogFactory &&
      IsInteractive() &&
      !ShowInterface(parent, dialogFactory, IsBatchProcessing()) )
   {
      return false;
   }

   bool returnVal = true;
   bool skipFlag = CheckWhetherSkipEffect();
   if (skipFlag == false)
   {
      auto name = GetName();
      ProgressDialog progress{
         name,
         XO("Applying %s...").Format( name ),
         pdlgHideStopButton
      };
      auto vr = valueRestorer( mProgress, &progress );

      returnVal = Process();
   }

   if (returnVal && (mT1 >= mT0 ))
   {
      selectedRegion.setTimes(mT0, mT1);
   }

   success = returnVal;
   return returnVal;
}

bool Effect::Delegate(
   Effect &delegate, wxWindow &parent, const EffectDialogFactory &factory )
{
   NotifyingSelectedRegion region;
   region.setTimes( mT0, mT1 );

   return delegate.DoEffect( parent, mProjectRate, mTracks, mFactory,
      region, factory );
}

// All legacy effects should have this overridden
bool Effect::Init()
{
   return true;
}

int Effect::GetPass()
{
   return mPass;
}

bool Effect::InitPass1()
{
   return true;
}

bool Effect::InitPass2()
{
   return false;
}

bool Effect::Process()
{
   CopyInputTracks(true);
   bool bGoodResult = true;

   // It's possible that the number of channels the effect expects changed based on
   // the parameters (the Audacity Reverb effect does when the stereo width is 0).
   mNumAudioIn = GetAudioInCount();
   mNumAudioOut = GetAudioOutCount();

   mPass = 1;
   if (InitPass1())
   {
      bGoodResult = ProcessPass();
      mPass = 2;
      if (bGoodResult && InitPass2())
      {
         bGoodResult = ProcessPass();
      }
   }

   ReplaceProcessedTracks(bGoodResult);

   return bGoodResult;
}

bool Effect::ProcessPass()
{
   bool bGoodResult = true;
   bool isGenerator = GetType() == EffectTypeGenerate;

   FloatBuffers inBuffer, outBuffer;
   ArrayOf<float *> inBufPos, outBufPos;

   ChannelName map[3];

   mBufferSize = 0;
   mBlockSize = 0;

   int count = 0;
   bool clear = false;

   const bool multichannel = mNumAudioIn > 1;
   auto range = multichannel
      ? mOutputTracks->Leaders()
      : mOutputTracks->Any();
   range.VisitWhile( bGoodResult,
      [&](WaveTrack *left, const Track::Fallthrough &fallthrough) {
         if (!left->GetSelected())
            return fallthrough();

         sampleCount len;
         sampleCount leftStart;
         sampleCount rightStart = 0;

         if (!isGenerator)
         {
            GetSamples(left, &leftStart, &len);
            mSampleCnt = len;
         }
         else
         {
            len = 0;
            leftStart = 0;
            mSampleCnt = left->TimeToLongSamples(mDuration);
         }

         mNumChannels = 0;
         WaveTrack *right{};

         // Iterate either over one track which could be any channel,
         // or if multichannel, then over all channels of left,
         // which is a leader.
         for (auto channel :
              TrackList::Channels(left).StartingWith(left)) {
            if (channel->GetChannel() == Track::LeftChannel)
               map[mNumChannels] = ChannelNameFrontLeft;
            else if (channel->GetChannel() == Track::RightChannel)
               map[mNumChannels] = ChannelNameFrontRight;
            else
               map[mNumChannels] = ChannelNameMono;

            ++ mNumChannels;
            map[mNumChannels] = ChannelNameEOL;

            if (! multichannel)
               break;

            if (mNumChannels == 2) {
               // TODO: more-than-two-channels
               right = channel;
               clear = false;
               if (!isGenerator)
                  GetSamples(right, &rightStart, &len);

               // Ignore other channels
               break;
            }
         }

         // Let the client know the sample rate
         SetSampleRate(left->GetRate());

         // Get the block size the client wants to use
         auto max = left->GetMaxBlockSize() * 2;
         mBlockSize = SetBlockSize(max);

         // Calculate the buffer size to be at least the max rounded up to the clients
         // selected block size.
         const auto prevBufferSize = mBufferSize;
         mBufferSize = ((max + (mBlockSize - 1)) / mBlockSize) * mBlockSize;

         // If the buffer size has changed, then (re)allocate the buffers
         if (prevBufferSize != mBufferSize)
         {
            // Always create the number of input buffers the client expects even if we don't have
            // the same number of channels.
            inBufPos.reinit( mNumAudioIn );
            inBuffer.reinit( mNumAudioIn, mBufferSize );

            // We won't be using more than the first 2 buffers, so clear the rest (if any)
            for (size_t i = 2; i < mNumAudioIn; i++)
            {
               for (size_t j = 0; j < mBufferSize; j++)
               {
                  inBuffer[i][j] = 0.0;
               }
            }

            // Always create the number of output buffers the client expects even if we don't have
            // the same number of channels.
            outBufPos.reinit( mNumAudioOut );
            // Output buffers get an extra mBlockSize worth to give extra room if
            // the plugin adds latency
            outBuffer.reinit( mNumAudioOut, mBufferSize + mBlockSize );
         }

         // (Re)Set the input buffer positions
         for (size_t i = 0; i < mNumAudioIn; i++)
         {
            inBufPos[i] = inBuffer[i].get();
         }

         // (Re)Set the output buffer positions
         for (size_t i = 0; i < mNumAudioOut; i++)
         {
            outBufPos[i] = outBuffer[i].get();
         }

         // Clear unused input buffers
         if (!right && !clear && mNumAudioIn > 1)
         {
            for (size_t j = 0; j < mBufferSize; j++)
            {
               inBuffer[1][j] = 0.0;
            }
            clear = true;
         }

         // Go process the track(s)
         bGoodResult = ProcessTrack(
            count, map, left, right, leftStart, rightStart, len,
            inBuffer, outBuffer, inBufPos, outBufPos);
         if (!bGoodResult)
            return;

         count++;
      },
      [&](Track *t) {
         if (t->IsSyncLockSelected())
            t->SyncLockAdjust(mT1, mT0 + mDuration);
      }
   );

   if (bGoodResult && GetType() == EffectTypeGenerate)
   {
      mT1 = mT0 + mDuration;
   }

   return bGoodResult;
}

bool Effect::ProcessTrack(int count,
                          ChannelNames map,
                          WaveTrack *left,
                          WaveTrack *right,
                          sampleCount leftStart,
                          sampleCount rightStart,
                          sampleCount len,
                          FloatBuffers &inBuffer,
                          FloatBuffers &outBuffer,
                          ArrayOf< float * > &inBufPos,
                          ArrayOf< float *> &outBufPos)
{
   bool rc = true;

   // Give the plugin a chance to initialize
   if (!ProcessInitialize(len, map))
   {
      return false;
   }

   { // Start scope for cleanup
   auto cleanup = finally( [&] {
      // Allow the plugin to cleanup
      if (!ProcessFinalize())
      {
         // In case of non-exceptional flow of control, set rc
         rc = false;
      }
   } );

   // For each input block of samples, we pass it to the effect along with a
   // variable output location.  This output location is simply a pointer into a
   // much larger buffer.  This reduces the number of calls required to add the
   // samples to the output track.
   //
   // Upon return from the effect, the output samples are "moved to the left" by
   // the number of samples in the current latency setting, effectively removing any
   // delay introduced by the effect.
   //
   // At the same time the total number of delayed samples are gathered and when
   // there is no further input data to process, the loop continues to call the
   // effect with an empty input buffer until the effect has had a chance to
   // return all of the remaining delayed samples.
   auto inLeftPos = leftStart;
   auto inRightPos = rightStart;
   auto outLeftPos = leftStart;
   auto outRightPos = rightStart;

   auto inputRemaining = len;
   decltype(GetLatency()) curDelay = 0, delayRemaining = 0;
   decltype(mBlockSize) curBlockSize = 0;

   decltype(mBufferSize) inputBufferCnt = 0;
   decltype(mBufferSize) outputBufferCnt = 0;
   bool cleared = false;

   auto chans = std::min<unsigned>(mNumAudioOut, mNumChannels);

   std::shared_ptr<WaveTrack> genLeft, genRight;

   decltype(len) genLength = 0;
   bool isGenerator = GetType() == EffectTypeGenerate;
   bool isProcessor = GetType() == EffectTypeProcess;
   double genDur = 0;
   if (isGenerator)
   {
      if (mIsPreview) {
         gPrefs->Read(wxT("/AudioIO/EffectsPreviewLen"), &genDur, 6.0);
         genDur = wxMin(mDuration, CalcPreviewInputLength(genDur));
      }
      else {
         genDur = mDuration;
      }

      genLength = sampleCount((left->GetRate() * genDur) + 0.5);  // round to nearest sample
      delayRemaining = genLength;
      cleared = true;

      // Create temporary tracks
      genLeft = mFactory->NewWaveTrack(left->GetSampleFormat(), left->GetRate());

      if (right)
         genRight = mFactory->NewWaveTrack(right->GetSampleFormat(), right->GetRate());
   }

   // Call the effect until we run out of input or delayed samples
   while (inputRemaining != 0 || delayRemaining != 0)
   {
      // Still working on the input samples
      if (inputRemaining != 0)
      {
         // Need to refill the input buffers
         if (inputBufferCnt == 0)
         {
            // Calculate the number of samples to get
            inputBufferCnt =
               limitSampleBufferSize( mBufferSize, inputRemaining );

            // Fill the input buffers
            left->Get((samplePtr) inBuffer[0].get(), floatSample, inLeftPos, inputBufferCnt);
            if (right)
            {
               right->Get((samplePtr) inBuffer[1].get(), floatSample, inRightPos, inputBufferCnt);
            }

            // Reset the input buffer positions
            for (size_t i = 0; i < mNumChannels; i++)
            {
               inBufPos[i] = inBuffer[i].get();
            }
         }

         // Calculate the number of samples to process
         curBlockSize = mBlockSize;
         if (curBlockSize > inputRemaining)
         {
            // We've reached the last block...set current block size to what's left
            // inputRemaining is positive and bounded by a size_t
            curBlockSize = inputRemaining.as_size_t();
            inputRemaining = 0;

            // Clear the remainder of the buffers so that a full block can be passed
            // to the effect
            auto cnt = mBlockSize - curBlockSize;
            for (size_t i = 0; i < mNumChannels; i++)
            {
               for (decltype(cnt) j = 0 ; j < cnt; j++)
               {
                  inBufPos[i][j + curBlockSize] = 0.0;
               }
            }

            // Might be able to use up some of the delayed samples
            if (delayRemaining != 0)
            {
               // Don't use more than needed
               cnt = limitSampleBufferSize(cnt, delayRemaining);
               delayRemaining -= cnt;
               curBlockSize += cnt;
            }
         }
      }
      // We've exhausted the input samples and are now working on the delay
      else if (delayRemaining != 0)
      {
         // Calculate the number of samples to process
         curBlockSize = limitSampleBufferSize( mBlockSize, delayRemaining );
         delayRemaining -= curBlockSize;

         // From this point on, we only want to feed zeros to the plugin
         if (!cleared)
         {
            // Reset the input buffer positions
            for (size_t i = 0; i < mNumChannels; i++)
            {
               inBufPos[i] = inBuffer[i].get();

               // And clear
               for (size_t j = 0; j < mBlockSize; j++)
               {
                  inBuffer[i][j] = 0.0;
               }
            }
            cleared = true;
         }
      }

      // Finally call the plugin to process the block
      decltype(curBlockSize) processed;
      try
      {
         processed = ProcessBlock(inBufPos.get(), outBufPos.get(), curBlockSize);
      }
      catch( const AudacityException & WXUNUSED(e) )
      {
         // PRL: Bug 437:
         // Pass this along to our application-level handler
         throw;
      }
      catch(...)
      {
         // PRL:
         // Exceptions for other reasons, maybe in third-party code...
         // Continue treating them as we used to, but I wonder if these
         // should now be treated the same way.
         return false;
      }
      wxASSERT(processed == curBlockSize);
      wxUnusedVar(processed);

      // Bump to next input buffer position
      if (inputRemaining != 0)
      {
         for (size_t i = 0; i < mNumChannels; i++)
         {
            inBufPos[i] += curBlockSize;
         }
         inputRemaining -= curBlockSize;
         inputBufferCnt -= curBlockSize;
      }

      // "ls" and "rs" serve as the input sample index for the left and
      // right channels when processing the input samples.  If we flip
      // over to processing delayed samples, they simply become counters
      // for the progress display.
      inLeftPos += curBlockSize;
      inRightPos += curBlockSize;

      // Get the current number of delayed samples and accumulate
      if (isProcessor)
      {
         {
            auto delay = GetLatency();
            curDelay += delay;
            delayRemaining += delay;
         }

         // If the plugin has delayed the output by more samples than our current
         // block size, then we leave the output pointers alone.  This effectively
         // removes those delayed samples from the output buffer.
         if (curDelay >= curBlockSize)
         {
            curDelay -= curBlockSize;
            curBlockSize = 0;
         }
         // We have some delayed samples, at the beginning of the output samples,
         // so overlay them by shifting the remaining output samples.
         else if (curDelay > 0)
         {
            // curDelay is bounded by curBlockSize:
            auto delay = curDelay.as_size_t();
            curBlockSize -= delay;
            for (size_t i = 0; i < chans; i++)
            {
               memmove(outBufPos[i], outBufPos[i] + delay, sizeof(float) * curBlockSize);
            }
            curDelay = 0;
         }
      }

      // Adjust the number of samples in the output buffers
      outputBufferCnt += curBlockSize;

      // Still have room in the output buffers
      if (outputBufferCnt < mBufferSize)
      {
         // Bump to next output buffer position
         for (size_t i = 0; i < chans; i++)
         {
            outBufPos[i] += curBlockSize;
         }
      }
      // Output buffers have filled
      else
      {
         if (isProcessor)
         {
            // Write them out
            left->Set((samplePtr) outBuffer[0].get(), floatSample, outLeftPos, outputBufferCnt);
            if (right)
            {
               if (chans >= 2)
               {
                  right->Set((samplePtr) outBuffer[1].get(), floatSample, outRightPos, outputBufferCnt);
               }
               else
               {
                  right->Set((samplePtr) outBuffer[0].get(), floatSample, outRightPos, outputBufferCnt);
               }
            }
         }
         else if (isGenerator)
         {
            genLeft->Append((samplePtr) outBuffer[0].get(), floatSample, outputBufferCnt);
            if (genRight)
            {
               genRight->Append((samplePtr) outBuffer[1].get(), floatSample, outputBufferCnt);
            }
         }

         // Reset the output buffer positions
         for (size_t i = 0; i < chans; i++)
         {
            outBufPos[i] = outBuffer[i].get();
         }

         // Bump to the next track position
         outLeftPos += outputBufferCnt;
         outRightPos += outputBufferCnt;
         outputBufferCnt = 0;
      }

      if (mNumChannels > 1)
      {
         if (TrackGroupProgress(count,
               (inLeftPos - leftStart).as_double() /
               (isGenerator ? genLength : len).as_double()))
         {
            rc = false;
            break;
         }
      }
      else
      {
         if (TrackProgress(count,
               (inLeftPos - leftStart).as_double() /
               (isGenerator ? genLength : len).as_double()))
         {
            rc = false;
            break;
         }
      }
   }

   // Put any remaining output
   if (rc && outputBufferCnt)
   {
      if (isProcessor)
      {
         left->Set((samplePtr) outBuffer[0].get(), floatSample, outLeftPos, outputBufferCnt);
         if (right)
         {
            if (chans >= 2)
            {
               right->Set((samplePtr) outBuffer[1].get(), floatSample, outRightPos, outputBufferCnt);
            }
            else
            {
               right->Set((samplePtr) outBuffer[0].get(), floatSample, outRightPos, outputBufferCnt);
            }
         }
      }
      else if (isGenerator)
      {
         genLeft->Append((samplePtr) outBuffer[0].get(), floatSample, outputBufferCnt);
         if (genRight)
         {
            genRight->Append((samplePtr) outBuffer[1].get(), floatSample, outputBufferCnt);
         }
      }
   }

   if (rc && isGenerator)
   {
      auto pProject = FindProject();

      // PRL:  this code was here and could not have been the right
      // intent, mixing time and sampleCount values:
      // StepTimeWarper warper(mT0 + genLength, genLength - (mT1 - mT0));

      // This looks like what it should have been:
      // StepTimeWarper warper(mT0 + genDur, genDur - (mT1 - mT0));
      // But rather than fix it, I will just disable the use of it for now.
      // The purpose was to remap split lines inside the selected region when
      // a generator replaces it with sound of different duration.  But
      // the "correct" version might have the effect of mapping some splits too
      // far left, to before the selection.
      // In practice the wrong version probably did nothing most of the time,
      // because the cutoff time for the step time warper was 44100 times too
      // far from mT0.

      // Transfer the data from the temporary tracks to the actual ones
      genLeft->Flush();
      // mT1 gives us the NEW selection. We want to replace up to GetSel1().
      auto &selectedRegion = ViewInfo::Get( *pProject ).selectedRegion;
      left->ClearAndPaste(mT0,
         selectedRegion.t1(), genLeft.get(), true, true,
         nullptr /* &warper */);

      if (genRight)
      {
         genRight->Flush();
         right->ClearAndPaste(mT0, mT1, genRight.get(), true, true,
                              nullptr /* &warper */);
      }
   }

   } // End scope for cleanup
   return rc;
}

void Effect::End()
{
}

void Effect::PopulateOrExchange(ShuttleGui & WXUNUSED(S))
{
   return;
}

bool Effect::TransferDataToWindow()
{
   return true;
}

bool Effect::TransferDataFromWindow()
{
   return true;
}

bool Effect::EnableApply(bool enable)
{
   // May be called during initialization, so try to find the dialog
   wxWindow *dlg = mUIDialog;
   if (!dlg && mUIParent)
   {
      dlg = wxGetTopLevelParent(mUIParent);
   }

   if (dlg)
   {
      wxWindow *apply = dlg->FindWindow(wxID_APPLY);

      // Don't allow focus to get trapped
      if (!enable)
      {
         wxWindow *focus = dlg->FindFocus();
         if (focus == apply)
         {
            dlg->FindWindow(wxID_CLOSE)->SetFocus();
         }
      }

      apply->Enable(enable);
   }

   EnablePreview(enable);

   return enable;
}

bool Effect::EnablePreview(bool enable)
{
   // May be called during initialization, so try to find the dialog
   wxWindow *dlg = mUIDialog;
   if (!dlg && mUIParent)
   {
      dlg = wxGetTopLevelParent(mUIParent);
   }

   if (dlg)
   {
      wxWindow *play = dlg->FindWindow(kPlayID);
      if (play)
      {
         wxWindow *rewind = dlg->FindWindow(kRewindID);
         wxWindow *ffwd = dlg->FindWindow(kFFwdID);

         // Don't allow focus to get trapped
         if (!enable)
         {
            wxWindow *focus = dlg->FindFocus();
            if (focus && (focus == play || focus == rewind || focus == ffwd))
            {
               dlg->FindWindow(wxID_CLOSE)->SetFocus();
            }
         }

         play->Enable(enable);
         if (SupportsRealtime())
         {
            rewind->Enable(enable);
            ffwd->Enable(enable);
         }
      }
   }

   return enable;
}

void Effect::EnableDebug(bool enable)
{
   mUIDebug = enable;
}

void Effect::SetLinearEffectFlag(bool linearEffectFlag)
{
   mIsLinearEffect = linearEffectFlag;
}

void Effect::SetPreviewFullSelectionFlag(bool previewDurationFlag)
{
   mPreviewFullSelection = previewDurationFlag;
}


void Effect::IncludeNotSelectedPreviewTracks(bool includeNotSelected)
{
   mPreviewWithNotSelected = includeNotSelected;
}

bool Effect::TotalProgress(double frac, const TranslatableString &msg)
{
   auto updateResult = (mProgress ?
      mProgress->Update(frac, msg) :
      ProgressResult::Success);
   return (updateResult != ProgressResult::Success);
}

bool Effect::TrackProgress(int whichTrack, double frac, const TranslatableString &msg)
{
   auto updateResult = (mProgress ?
      mProgress->Update(whichTrack + frac, (double) mNumTracks, msg) :
      ProgressResult::Success);
   return (updateResult != ProgressResult::Success);
}

bool Effect::TrackGroupProgress(int whichGroup, double frac, const TranslatableString &msg)
{
   auto updateResult = (mProgress ?
      mProgress->Update(whichGroup + frac, (double) mNumGroups, msg) :
      ProgressResult::Success);
   return (updateResult != ProgressResult::Success);
}

void Effect::GetSamples(
   const WaveTrack *track, sampleCount *start, sampleCount *len)
{
   double trackStart = track->GetStartTime();
   double trackEnd = track->GetEndTime();
   double t0 = mT0 < trackStart ? trackStart : mT0;
   double t1 = mT1 > trackEnd ? trackEnd : mT1;

#if 0
   if (GetType() & INSERT_EFFECT) {
      t1 = t0 + mDuration;
      if (mT0 == mT1) {
         // Not really part of the calculation, but convenient to put here
         track->InsertSilence(t0, t1);
      }
   }
#endif

   if (t1 > t0) {
      *start = track->TimeToLongSamples(t0);
      auto end = track->TimeToLongSamples(t1);
      *len = end - *start;
   }
   else {
      *start = 0;
      *len  = 0;
   }
}

//
// private methods
//
// Use this method to copy the input tracks to mOutputTracks, if
// doing the processing on them, and replacing the originals only on success (and not cancel).
// Copy the group tracks that have tracks selected
// If not all sync-locked selected, then only selected wave tracks.
void Effect::CopyInputTracks(bool allSyncLockSelected)
{
   // Reset map
   mIMap.clear();
   mOMap.clear();

   mOutputTracks = TrackList::Create( nullptr );

   auto trackRange = mTracks->Any() +
      [&] (const Track *pTrack) {
         return allSyncLockSelected
         ? pTrack->IsSelectedOrSyncLockSelected()
         : track_cast<const WaveTrack*>( pTrack ) && pTrack->GetSelected();
      };

   t2bHash added;

   for (auto aTrack : trackRange)
   {
      Track *o = mOutputTracks->Add(aTrack->Duplicate());
      mIMap.push_back(aTrack);
      mOMap.push_back(o);
   }
}

Track *Effect::AddToOutputTracks(const std::shared_ptr<Track> &t)
{
   mIMap.push_back(NULL);
   mOMap.push_back(t.get());
   return mOutputTracks->Add(t);
}

Effect::AddedAnalysisTrack::AddedAnalysisTrack(Effect *pEffect, const wxString &name)
   : mpEffect(pEffect)
{
   LabelTrack::Holder pTrack{ pEffect->mFactory->NewLabelTrack() };
   mpTrack = pTrack.get();
   if (!name.empty())
      pTrack->SetName(name);
   pEffect->mTracks->Add( pTrack );
}

Effect::AddedAnalysisTrack::AddedAnalysisTrack(AddedAnalysisTrack &&that)
{
   mpEffect = that.mpEffect;
   mpTrack = that.mpTrack;
   that.Commit();
}

void Effect::AddedAnalysisTrack::Commit()
{
   mpEffect = nullptr;
}

Effect::AddedAnalysisTrack::~AddedAnalysisTrack()
{
   if (mpEffect) {
      // not committed -- DELETE the label track
      mpEffect->mTracks->Remove(mpTrack);
   }
}

auto Effect::AddAnalysisTrack(const wxString &name) -> std::shared_ptr<AddedAnalysisTrack>
{
   return std::shared_ptr<AddedAnalysisTrack>
      { safenew AddedAnalysisTrack{ this, name } };
}

Effect::ModifiedAnalysisTrack::ModifiedAnalysisTrack
   (Effect *pEffect, const LabelTrack *pOrigTrack, const wxString &name)
   : mpEffect(pEffect)
{
   // copy LabelTrack here, so it can be undone on cancel
   auto newTrack = pOrigTrack->Copy(pOrigTrack->GetStartTime(), pOrigTrack->GetEndTime());

   mpTrack = static_cast<LabelTrack*>(newTrack.get());

   // Why doesn't LabelTrack::Copy complete the job? :
   mpTrack->SetOffset(pOrigTrack->GetStartTime());
   if (!name.empty())
      mpTrack->SetName(name);

   // mpOrigTrack came from mTracks which we own but expose as const to subclasses
   // So it's okay that we cast it back to const
   mpOrigTrack =
      pEffect->mTracks->Replace(const_cast<LabelTrack*>(pOrigTrack),
         newTrack );
}

Effect::ModifiedAnalysisTrack::ModifiedAnalysisTrack(ModifiedAnalysisTrack &&that)
{
   mpEffect = that.mpEffect;
   mpTrack = that.mpTrack;
   mpOrigTrack = std::move(that.mpOrigTrack);
   that.Commit();
}

void Effect::ModifiedAnalysisTrack::Commit()
{
   mpEffect = nullptr;
}

Effect::ModifiedAnalysisTrack::~ModifiedAnalysisTrack()
{
   if (mpEffect) {
      // not committed -- DELETE the label track
      // mpOrigTrack came from mTracks which we own but expose as const to subclasses
      // So it's okay that we cast it back to const
      mpEffect->mTracks->Replace(mpTrack, mpOrigTrack);
   }
}

auto Effect::ModifyAnalysisTrack
   (const LabelTrack *pOrigTrack, const wxString &name) -> ModifiedAnalysisTrack
{
   return{ this, pOrigTrack, name };
}

// If bGoodResult, replace mTracks tracks with successfully processed mOutputTracks copies.
// Else clear and DELETE mOutputTracks copies.
void Effect::ReplaceProcessedTracks(const bool bGoodResult)
{
   if (!bGoodResult) {
      // Free resources, unless already freed.

      // Processing failed or was cancelled so throw away the processed tracks.
      if ( mOutputTracks )
         mOutputTracks->Clear();

      // Reset map
      mIMap.clear();
      mOMap.clear();

      //TODO:undo the non-gui ODTask transfer
      return;
   }

   // Assume resources need to be freed.
   wxASSERT(mOutputTracks); // Make sure we at least did the CopyInputTracks().

   auto iterOut = mOutputTracks->ListOfTracks::begin(),
      iterEnd = mOutputTracks->ListOfTracks::end();

   size_t cnt = mOMap.size();
   size_t i = 0;

   for (; iterOut != iterEnd; ++i) {
      ListOfTracks::value_type o = *iterOut;
      // If tracks were removed from mOutputTracks, then there will be
      // tracks in the map that must be removed from mTracks.
      while (i < cnt && mOMap[i] != o.get()) {
         const auto t = mIMap[i];
         if (t) {
            mTracks->Remove(t);
         }
         i++;
      }

      // This should never happen
      wxASSERT(i < cnt);

      // Remove the track from the output list...don't DELETE it
      iterOut = mOutputTracks->erase(iterOut);

      const auto  t = mIMap[i];
      if (t == NULL)
      {
         // This track is a NEW addition to output tracks; add it to mTracks
         mTracks->Add( o );
      }
      else
      {
         // Replace mTracks entry with the NEW track
         mTracks->Replace(t, o);

         // If the track is a wave track,
         // Swap the wavecache track the ondemand task uses, since now the NEW
         // one will be kept in the project
         if (ODManager::IsInstanceCreated()) {
            ODManager::Instance()->ReplaceWaveTrack( t, o );
         }
      }
   }

   // If tracks were removed from mOutputTracks, then there may be tracks
   // left at the end of the map that must be removed from mTracks.
   while (i < cnt) {
      const auto t = mIMap[i];
      if (t) {
         mTracks->Remove(t);
      }
      i++;
   }

   // Reset map
   mIMap.clear();
   mOMap.clear();

   // Make sure we processed everything
   wxASSERT(mOutputTracks->empty());

   // The output list is no longer needed
   mOutputTracks.reset();
   nEffectsDone++;
}

const AudacityProject *Effect::FindProject() const
{
   return inputTracks()->GetOwner();
}

void Effect::CountWaveTracks()
{
   mNumTracks = mTracks->Selected< const WaveTrack >().size();
   mNumGroups = mTracks->SelectedLeaders< const WaveTrack >().size();
}

double Effect::CalcPreviewInputLength(double previewLength)
{
   return previewLength;
}

bool Effect::IsHidden()
{
   return false;
}

void Effect::Preview(bool dryOnly)
{
   if (mNumTracks == 0) { // nothing to preview
      return;
   }

   auto gAudioIO = AudioIO::Get();
   if (gAudioIO->IsBusy()) {
      return;
   }

   wxWindow *FocusDialog = wxWindow::FindFocus();

   double previewDuration;
   bool isNyquist = GetFamily() == NYQUISTEFFECTS_FAMILY;
   bool isGenerator = GetType() == EffectTypeGenerate;

   // Mix a few seconds of audio from all of the tracks
   double previewLen;
   gPrefs->Read(wxT("/AudioIO/EffectsPreviewLen"), &previewLen, 6.0);

   const double rate = mProjectRate;

   if (isNyquist && isGenerator) {
      previewDuration = CalcPreviewInputLength(previewLen);
   }
   else {
      previewDuration = wxMin(mDuration, CalcPreviewInputLength(previewLen));
   }

   double t1 = mT0 + previewDuration;

   if ((t1 > mT1) && !isGenerator) {
      t1 = mT1;
   }

   if (t1 <= mT0)
      return;

   bool success = true;

   auto cleanup = finally( [&] {

      // Effect is already inited; we will call Process, End, and then Init
      // again, so the state is exactly the way it was before Preview
      // was called.
      if (!dryOnly) {
         End();
         GuardedCall( [&]{ Init(); } );
      }
   } );

   auto vr0 = valueRestorer( mT0 );
   auto vr1 = valueRestorer( mT1 );
   // Most effects should stop at t1.
   if (!mPreviewFullSelection)
      mT1 = t1;

   // Save the original track list
   TrackList *saveTracks = mTracks;

   auto cleanup2 = finally( [&] {
      mTracks = saveTracks;
      if (FocusDialog) {
         FocusDialog->SetFocus();
      }

      // In case of failed effect, be sure to free memory.
      ReplaceProcessedTracks( false );
   } );

   // Build NEW tracklist from rendering tracks
   // Set the same owning project, so FindProject() can see it within Process()
   const auto pProject = saveTracks->GetOwner();
   auto uTracks = TrackList::Create( pProject );
   mTracks = uTracks.get();

   // Linear Effect preview optimised by pre-mixing to one track.
   // Generators need to generate per track.
   if (mIsLinearEffect && !isGenerator) {
      WaveTrack::Holder mixLeft, mixRight;
      MixAndRender(saveTracks, mFactory, rate, floatSample, mT0, t1, mixLeft, mixRight);
      if (!mixLeft)
         return;

      mixLeft->Offset(-mixLeft->GetStartTime());
      mixLeft->SetSelected(true);
      WaveTrackView::Get( *mixLeft )
         .SetDisplay(WaveTrackViewConstants::NoDisplay);
      auto pLeft = mTracks->Add( mixLeft );
      Track *pRight{};
      if (mixRight) {
         mixRight->Offset(-mixRight->GetStartTime());
         mixRight->SetSelected(true);
         pRight = mTracks->Add( mixRight );
      }
      mTracks->GroupChannels(*pLeft, pRight ? 2 : 1);
   }
   else {
      for (auto src : saveTracks->Any< const WaveTrack >()) {
         if (src->GetSelected() || mPreviewWithNotSelected) {
            auto dest = src->Copy(mT0, t1);
            dest->SetSelected(src->GetSelected());
            WaveTrackView::Get( *static_cast<WaveTrack*>(dest.get()) )
               .SetDisplay(WaveTrackViewConstants::NoDisplay);
            mTracks->Add( dest );
         }
      }
   }

   // NEW tracks start at time zero.
   // Adjust mT0 and mT1 to be the times to process, and to
   // play back in these tracks
   mT1 -= mT0;
   mT0 = 0.0;

   // Update track/group counts
   CountWaveTracks();

   // Apply effect
   if (!dryOnly) {
      ProgressDialog progress{
         GetName(),
         XO("Preparing preview"),
         pdlgHideCancelButton
      }; // Have only "Stop" button.
      auto vr = valueRestorer( mProgress, &progress );

      auto vr2 = valueRestorer( mIsPreview, true );

      success = Process();
   }

   if (success)
   {
      auto tracks = ProjectAudioManager::GetAllPlaybackTracks(*mTracks, true);

      // Some effects (Paulstretch) may need to generate more
      // than previewLen, so take the min.
      t1 = std::min(mT0 + previewLen, mT1);

      // Start audio playing
      AudioIOStartStreamOptions options { pProject, rate };
      int token =
         gAudioIO->StartStream(tracks, mT0, t1, options);

      if (token) {
         auto previewing = ProgressResult::Success;
         // The progress dialog must be deleted before stopping the stream
         // to allow events to flow to the app during StopStream processing.
         // The progress dialog blocks these events.
         {
            ProgressDialog progress
            (GetName(), XO("Previewing"), pdlgHideCancelButton);

            while (gAudioIO->IsStreamActive(token) && previewing == ProgressResult::Success) {
               ::wxMilliSleep(100);
               previewing = progress.Update(gAudioIO->GetStreamTime() - mT0, t1 - mT0);
            }
         }

         gAudioIO->StopStream();

         while (gAudioIO->IsBusy()) {
            ::wxMilliSleep(100);
         }
      }
      else {
         ShowErrorDialog(FocusDialog, XO("Error"),
                         XO("Error opening sound device.\nTry changing the audio host, playback device and the project sample rate."),
                         wxT("Error_opening_sound_device"));
      }
   }
}

int Effect::MessageBox( const TranslatableString& message,
   long style, const TranslatableString &titleStr)
{
   auto title = titleStr.empty()
      ? GetName()
      : XO("%s: %s").Format( GetName(), titleStr );
   return AudacityMessageBox( message, title, style, mUIParent );
}

BEGIN_EVENT_TABLE(EffectDialog, wxDialogWrapper)
   EVT_BUTTON(wxID_OK, EffectDialog::OnOk)
END_EVENT_TABLE()

EffectDialog::EffectDialog(wxWindow * parent,
                           const TranslatableString & title,
                           int type,
                           int flags,
                           int additionalButtons)
: wxDialogWrapper(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, flags)
{
   mType = type;
   mAdditionalButtons = additionalButtons;
}

void EffectDialog::Init()
{
   long buttons = eOkButton;
   if ((mType != EffectTypeAnalyze) && (mType != EffectTypeTool))
   {
      buttons |= eCancelButton;
      if (mType == EffectTypeProcess)
      {
         buttons |= ePreviewButton;
      }
   }

   ShuttleGui S(this, eIsCreating);

   S.SetBorder(5);
   S.StartVerticalLay(true);
   {
      PopulateOrExchange(S);
      S.AddStandardButtons(buttons|mAdditionalButtons);
   }
   S.EndVerticalLay();

   Layout();
   Fit();
   SetMinSize(GetSize());
   Center();
}

/// This is a virtual function which will be overridden to
/// provide the actual parameters that we want for each
/// kind of dialog.
void EffectDialog::PopulateOrExchange(ShuttleGui & WXUNUSED(S))
{
   return;
}

bool EffectDialog::TransferDataToWindow()
{
   ShuttleGui S(this, eIsSettingToDialog);
   PopulateOrExchange(S);

   return true;
}

bool EffectDialog::TransferDataFromWindow()
{
   ShuttleGui S(this, eIsGettingFromDialog);
   PopulateOrExchange(S);

   return true;
}

bool EffectDialog::Validate()
{
   return true;
}

void EffectDialog::OnPreview(wxCommandEvent & WXUNUSED(evt))
{
   return;
}

void EffectDialog::OnOk(wxCommandEvent & WXUNUSED(evt))
{
   // On wxGTK (wx2.8.12), the default action is still executed even if
   // the button is disabled.  This appears to affect all wxDialogs, not
   // just our Effects dialogs.  So, this is a only temporary workaround
   // for legacy effects that disable the OK button.  Hopefully this has
   // been corrected in wx3.
   if (FindWindow(wxID_OK)->IsEnabled() && Validate() && TransferDataFromWindow())
   {
      EndModal(true);
   }

   return;
}

///////////////////////////////////////////////////////////////////////////////
//
// EffectPresetsDialog
//
///////////////////////////////////////////////////////////////////////////////

enum
{
   ID_Type = 10000
};

BEGIN_EVENT_TABLE(EffectPresetsDialog, wxDialogWrapper)
   EVT_CHOICE(ID_Type, EffectPresetsDialog::OnType)
   EVT_LISTBOX_DCLICK(wxID_ANY, EffectPresetsDialog::OnOk)
   EVT_BUTTON(wxID_OK, EffectPresetsDialog::OnOk)
   EVT_BUTTON(wxID_CANCEL, EffectPresetsDialog::OnCancel)
END_EVENT_TABLE()

EffectPresetsDialog::EffectPresetsDialog(wxWindow *parent, Effect *effect)
:  wxDialogWrapper(parent, wxID_ANY, XO("Select Preset"))
{
   ShuttleGui S(this, eIsCreating);
   S.StartVerticalLay();
   {
      S.StartTwoColumn();
      S.SetStretchyCol(1);
      {
         S.AddPrompt(XO("Type:"));
         mType = S.Id(ID_Type).AddChoice( {}, {}, 0 );

         S.AddPrompt(XO("&Preset:"));
         mPresets = S
            .Style( wxLB_SINGLE | wxLB_NEEDED_SB )
            .AddListBox( {} );
      }
      S.EndTwoColumn();

      S.AddStandardButtons();
   }
   S.EndVerticalLay();

   mUserPresets = effect->GetUserPresets();
   mFactoryPresets = effect->GetFactoryPresets();

   if (mUserPresets.size() > 0)
   {
      mType->Append(_("User Presets"));
   }

   if (mFactoryPresets.size() > 0)
   {
      mType->Append(_("Factory Presets"));
   }

   if (effect->HasCurrentSettings())
   {
      mType->Append(_("Current Settings"));
   }

   if (effect->HasFactoryDefaults())
   {
      mType->Append(_("Factory Defaults"));
   }

   UpdateUI();
}

EffectPresetsDialog::~EffectPresetsDialog()
{
}

wxString EffectPresetsDialog::GetSelected() const
{
   return mSelection;
}

void EffectPresetsDialog::SetSelected(const wxString & parms)
{
   wxString preset = parms;
   if (preset.StartsWith(Effect::kUserPresetIdent))
   {
      preset.Replace(Effect::kUserPresetIdent, wxEmptyString, false);
      SetPrefix(XO("User Presets"), preset);
   }
   else if (preset.StartsWith(Effect::kFactoryPresetIdent))
   {
      preset.Replace(Effect::kFactoryPresetIdent, wxEmptyString, false);
      SetPrefix(XO("Factory Presets"), preset);
   }
   else if (preset.StartsWith(Effect::kCurrentSettingsIdent))
   {
      SetPrefix(XO("Current Settings"), wxEmptyString);
   }
   else if (preset.StartsWith(Effect::kFactoryDefaultsIdent))
   {
      SetPrefix(XO("Factory Defaults"), wxEmptyString);
   }
}

void EffectPresetsDialog::SetPrefix(
   const TranslatableString & type, const wxString & prefix)
{
   mType->SetStringSelection(type.Translation());

   if (type == XO("User Presets"))
   {
      mPresets->Clear();
      for (const auto &preset : mUserPresets)
         mPresets->Append(preset);
      mPresets->Enable(true);
      mPresets->SetStringSelection(prefix);
      if (mPresets->GetSelection() == wxNOT_FOUND)
      {
         mPresets->SetSelection(0);
      }
      mSelection = Effect::kUserPresetIdent + mPresets->GetStringSelection();
   }
   else if (type == XO("Factory Presets"))
   {
      mPresets->Clear();
      for (size_t i = 0, cnt = mFactoryPresets.size(); i < cnt; i++)
      {
         auto label = mFactoryPresets[i];
         if (label.empty())
         {
            label = _("None");
         }
         mPresets->Append(label);
      }
      mPresets->Enable(true);
      mPresets->SetStringSelection(prefix);
      if (mPresets->GetSelection() == wxNOT_FOUND)
      {
         mPresets->SetSelection(0);
      }
      mSelection = Effect::kFactoryPresetIdent + mPresets->GetStringSelection();
   }
   else if (type == XO("Current Settings"))
   {
      mPresets->Clear();
      mPresets->Enable(false);
      mSelection = Effect::kCurrentSettingsIdent;
   }
   else if (type == XO("Factory Defaults"))
   {
      mPresets->Clear();
      mPresets->Enable(false);
      mSelection = Effect::kFactoryDefaultsIdent;
   }
}

void EffectPresetsDialog::UpdateUI()
{
   int selected = mType->GetSelection();
   if (selected == wxNOT_FOUND)
   {
      selected = 0;
      mType->SetSelection(selected);
   }
   wxString type = mType->GetString(selected);

   if (type == _("User Presets"))
   {
      selected = mPresets->GetSelection();
      if (selected == wxNOT_FOUND)
      {
         selected = 0;
      }

      mPresets->Clear();
      for (const auto &preset : mUserPresets)
         mPresets->Append(preset);
      mPresets->Enable(true);
      mPresets->SetSelection(selected);
      mSelection = Effect::kUserPresetIdent + mPresets->GetString(selected);
   }
   else if (type == _("Factory Presets"))
   {
      selected = mPresets->GetSelection();
      if (selected == wxNOT_FOUND)
      {
         selected = 0;
      }

      mPresets->Clear();
      for (size_t i = 0, cnt = mFactoryPresets.size(); i < cnt; i++)
      {
         auto label = mFactoryPresets[i];
         if (label.empty())
         {
            label = _("None");
         }
         mPresets->Append(label);
      }
      mPresets->Enable(true);
      mPresets->SetSelection(selected);
      mSelection = Effect::kFactoryPresetIdent + mPresets->GetString(selected);
   }
   else if (type == _("Current Settings"))
   {
      mPresets->Clear();
      mPresets->Enable(false);
      mSelection = Effect::kCurrentSettingsIdent;
   }
   else if (type == _("Factory Defaults"))
   {
      mPresets->Clear();
      mPresets->Enable(false);
      mSelection = Effect::kFactoryDefaultsIdent;
   }
}

void EffectPresetsDialog::OnType(wxCommandEvent & WXUNUSED(evt))
{
   UpdateUI();
}

void EffectPresetsDialog::OnOk(wxCommandEvent & WXUNUSED(evt))
{
   UpdateUI();

   EndModal(true);
}

void EffectPresetsDialog::OnCancel(wxCommandEvent & WXUNUSED(evt))
{
   mSelection = wxEmptyString;

   EndModal(false);
}
