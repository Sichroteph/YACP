#include "NearbyStatsSyncActivity.h"

#include <FontCacheManager.h>
#include <I18n.h>
#include <cstring>
#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/SyncedStatsStore.h"

using nearby_stats::State;
using nearby_stats::Error;

bool NearbyStatsSyncActivity::initialize() {
  session.reset();
  radio.end();
  nearby_stats::Mac local;
  if (!HalNearby::factoryMac(local.data()) || !nearby_stats::validMac(local) || !radio.begin(1)) {
    LOG_ERR("NSYNC","Nearby initialization failed");
    return false;
  }
  session.emplace(local,SETTINGS.getEffectiveDeviceName(),nearby_stats::Session::Hooks{this,readLocal,send,savePeer});
  return true;
}

void NearbyStatsSyncActivity::onEnter() {
  Activity::onEnter();
  if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
  resetUi();
  app.on(1,&NearbyStatsSyncActivity::navigationAction,this);
  app.setScreen(&NearbyStatsSyncActivity::navigationScreen,this);
  initialize();
  requestUpdate();
}

void NearbyStatsSyncActivity::onExit() {
  radio.end();
  session.reset();
  Activity::onExit();
}

bool NearbyStatsSyncActivity::canSync() const {
#ifdef SIMULATOR
  return false;
#else
  return !session || session->state() == State::Ready || session->state() == State::Synced ||
         session->state() == State::Error;
#endif
}

void NearbyStatsSyncActivity::loop() {
  RenderLock lock;
  touchCommand = -1;
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || touchCommand == 0) { finish(); return; }
  if (canSync() && (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || touchCommand == 1)) {
    if (initialize()) session->start(millis());
    requestUpdate();
    return;
  }
  if (!session) return;
  const State before = session->state();
  char previousName[21];
  memcpy(previousName,session->peerName(),sizeof(previousName));
  if (radio.overflowed()) {
    session->fail(Error::Transport);
    radio.end();
  } else {
    nearby_stats::Mac source;
    size_t size;
    for (unsigned i = 0; i < 4 && radio.poll(source.data(),packet.data(),packet.size(),size); ++i)
      session->receive(source,packet.data(),size,millis());
    session->tick(millis());
  }
  if (session->state() != before || strcmp(previousName,session->peerName())) {
    if (session->state() == State::Error) {
      LOG_ERR("NSYNC","Exchange failed: %u",static_cast<unsigned>(session->error()));
      radio.end();
    }
    requestUpdate();
  }
}

bool NearbyStatsSyncActivity::readLocal(void*, uint8_t* data, size_t& size) {
  const bool ok = nearby_stats::readLocalSnapshot(data,size);
  if (!ok) LOG_ERR("NSYNC","Local statistics unavailable or newer format");
  return ok;
}
bool NearbyStatsSyncActivity::send(void* context, const nearby_stats::Mac& peer, const uint8_t* data, size_t size) {
  return static_cast<NearbyStatsSyncActivity*>(context)->radio.send(peer.data(),data,size);
}
bool NearbyStatsSyncActivity::savePeer(void*, const nearby_stats::Mac& peer, const uint8_t* data, size_t size) {
  const bool ok = nearby_stats::savePeerSnapshot(peer,data,size);
  if (!ok) LOG_ERR("NSYNC","Could not save peer snapshot");
  return ok;
}

const char* NearbyStatsSyncActivity::statusText() const {
#ifdef SIMULATOR
  return tr(STR_NEARBY_STATS_SIMULATOR_UNAVAILABLE);
#else
  if (!session) return tr(STR_CONNECTION_FAILED);
  switch (session->state()) {
    case State::Ready: return tr(STR_NEARBY_STATS_READY);
    case State::Discovering: return tr(STR_NEARBY_STATS_SCANNING);
    case State::Syncing: return tr(STR_NEARBY_STATS_SYNCING);
    case State::Synced: return tr(STR_NEARBY_STATS_SYNCED);
    case State::Error:
      switch (session->error()) {
        case Error::Version: return tr(STR_NEARBY_STATS_VERSION_MISMATCH);
        case Error::Timeout: return tr(STR_ERROR_CONNECTION_TIMEOUT);
        case Error::LocalStats: return tr(STR_FILE_OPEN_FAILED);
        default: return tr(STR_SYNC_FAILED_MSG);
      }
  }
  return tr(STR_SYNC_FAILED_MSG);
#endif
}

void NearbyStatsSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer,true,false);
  GUI.drawHeader(renderer,Rect{safe.x,safe.y + metrics.topPadding,safe.width,metrics.headerHeight},
                 tr(STR_NEARBY_STATS_SYNC),nullptr,false);
  int y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  // Up to three short UI lines are allocated only when this maintenance screen repaints.
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID,statusText(),safe.width - 32,3)) {
    renderer.drawCenteredText(UI_10_FONT_ID,y,line.c_str());
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;
  }
  y += metrics.verticalSpacing;
  const char* name = session && session->peerName()[0] ? session->peerName() : SETTINGS.getEffectiveDeviceName();
  renderer.drawCenteredText(SMALL_FONT_ID,y,name);
  if (session && session->state() == State::Ready) {
    y += renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
    for (const auto& line : renderer.wrappedText(SMALL_FONT_ID,tr(STR_NEARBY_STATS_READY_HINT),safe.width - 32,3)) {
      renderer.drawCenteredText(SMALL_FONT_ID,y,line.c_str());
      y += renderer.getLineHeight(SMALL_FONT_ID) + 4;
    }
  }
  if (mappedInput.hasTouch()) renderUi();
  else {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK),canSync() ? tr(STR_NEARBY_STATS_SYNC_BUTTON) : "","","");
    GUI.drawButtonHints(renderer,labels.btn1,labels.btn2,labels.btn3,labels.btn4);
  }
  renderer.displayBuffer();
}

void NearbyStatsSyncActivity::navigationAction(const freeink::ui::ActionEvent& event, void* user) {
  static_cast<NearbyStatsSyncActivity*>(user)->touchCommand = event.value;
}
void NearbyStatsSyncActivity::navigationScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<NearbyStatsSyncActivity*>(user);
  const Rect safe = UITheme::getInstance().getScreenSafeArea(self->renderer,false,false);
  const int width = (safe.width - 20) / 2;
  for (int i = 0; i < (self->canSync() ? 2 : 1); ++i) {
    freeink::ui::ButtonProps props;
    props.label = i == 0 ? tr(STR_BACK) : tr(STR_NEARBY_STATS_SYNC_BUTTON);
    props.action = 1;
    props.value = static_cast<int16_t>(i);
    props.inputMask = freeink::ui::InputTouch;
    screen.button(props,freeink::ui::Rect{static_cast<int16_t>(safe.x + 10 + i * width),
                  static_cast<int16_t>(safe.y + safe.height - 58),static_cast<int16_t>(width - 4),52});
  }
}
