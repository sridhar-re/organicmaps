#include "cross_reference_api.hpp"

#include "metrics/eye.hpp"

#include "platform/http_client.hpp"
#include "platform/platform.hpp"
#include "platform/preferred_languages.hpp"
#include "platform/settings.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "3party/jansson/myjansson.hpp"

using namespace cross_reference;

using namespace std::chrono;

namespace
{
constexpr minutes kMinMinutesCountAfterBooking = minutes(5);
constexpr minutes kMaxMinutesCountAfterBooking = minutes(60);
constexpr hours kShowCrossReferenceNotRaterThan = hours(24);

std::array<std::string, 9> const kSupportedBookingTypes = {{"tourism-hotel", "tourism-apartment",
                                                            "tourism-camp_site", "tourism-chalet",
                                                            "tourism-guest_house", "tourism-hostel",
                                                            "tourism-motel", "tourism-resort",
                                                            "sponsored-booking"}};

bool NeedToShowImpl(eye::Eye::InfoType const & eyeInfo)
{
  auto const timeSinceLastShown =
      eye::Clock::now() - eyeInfo->m_crossReferences.m_lastTimeShownAfterBooking;
  auto const timeSinceLastTransitionToBooking =
      eye::Clock::now() - eyeInfo->m_crossReferences.m_transitionToBookingTime;

  return timeSinceLastTransitionToBooking >= kMinMinutesCountAfterBooking ||
         timeSinceLastTransitionToBooking <= kMaxMinutesCountAfterBooking ||
         timeSinceLastShown > kShowCrossReferenceNotRaterThan;
}

void ParseCityGallery(std::string const & src, cross_reference::CityGallery & result)
{
  base::Json root(src.c_str());
  auto const dataArray = json_object_get(root.get(), "data");

  auto const size = json_array_size(dataArray);

  result.reserve(size);
  for (size_t i = 0; i < size; ++i)
  {
    cross_reference::CityGalleryItem item;
    auto const obj = json_array_get(dataArray, i);
    FromJSONObject(obj, "name", item.m_name);
    FromJSONObject(obj, "url", item.m_url);

    auto const imageUrlObj = json_object_get(obj, "image_url");
    if (!json_is_null(imageUrlObj))
      FromJSON(imageUrlObj, item.m_imageUrl);

    FromJSONObject(obj, "access", item.m_access);

    auto const tierObj = json_object_get(obj, "tier");
    if (!json_is_null(tierObj))
      FromJSON(tierObj, item.m_tier);

    auto const authorObj = json_object_get(obj, "author");
    FromJSONObject(authorObj, "key_id", item.m_author.m_id);
    FromJSONObject(authorObj, "name", item.m_author.m_name);

    auto const luxCategoryObj = json_object_get(obj, "lux_category");

    auto const luxCategoryNameobj = json_object_get(luxCategoryObj, "name");
    if (!json_is_null(luxCategoryNameobj))
      FromJSON(luxCategoryNameobj, item.m_luxCategory.m_name);

    FromJSONObject(luxCategoryObj, "color", item.m_luxCategory.m_color);

    result.emplace_back(std::move(item));
  }
}

std::string MakeCityGalleryUrl(std::string const & baseUrl, std::string const & osmId,
                               std::string const & lang)
{
  return baseUrl + osmId + "/?lang=" + lang;
}

void GetCrossReferenceCityGalleryImpl(std::string const & baseUrl, std::string const & osmId,
                                      CityGalleryCallback const & cb)
{
  if (osmId.empty())
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [cb]() { cb({}); });
    return;
  }

  CityGallery result;
  std::string httpResult;
  if (!WebApi::GetCityGalleryByOsmId(baseUrl, osmId, languages::GetCurrentNorm(), httpResult))
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [cb]() { cb({}); });
    return;
  }

  try
  {
    ParseCityGallery(httpResult, result);
  }
  catch (base::Json::Exception const & e)
  {
    LOG(LERROR, (e.Msg()));
    result.clear();
  }

  GetPlatform().RunTask(Platform::Thread::Gui, [ cb, result = move(result) ]() { cb(result); });
}
}  // namespace

namespace cross_reference
{
// static
bool WebApi::GetCityGalleryByOsmId(std::string const & baseUrl, std::string const & osmId,
                                   std::string const & lang, std::string & result)
{
  platform::HttpClient request(MakeCityGalleryUrl(baseUrl, osmId, lang));
  return request.RunHttpRequest(result);
}

Api::Api(std::string const & baseUrl /* = "https://routes.maps.me/gallery/v1/city/" */)
  : m_baseUrl(baseUrl)
{
}

void Api::SetDelegate(std::unique_ptr<Delegate> delegate)
{
  m_delegate = std::move(delegate);
}

void Api::OnEnterForeground()
{
  m_bookingCrossReferenceAwaitingForId.clear();
  settings::TryGet("BookingCrossReferenceAwaitingForId", m_bookingCrossReferenceAwaitingForId);

  if (m_bookingCrossReferenceAwaitingForId.empty())
    return;

  auto const eyeInfo = eye::Eye::Instance().GetInfo();
  auto const timeSinceLastTransitionToBooking =
      eye::Clock::now() - eyeInfo->m_crossReferences.m_transitionToBookingTime;

  if (timeSinceLastTransitionToBooking < kMinMinutesCountAfterBooking ||
      timeSinceLastTransitionToBooking > kMaxMinutesCountAfterBooking)
  {
    settings::Delete("BookingCrossReferenceAwaitingForId");
    m_bookingCrossReferenceAwaitingForId.clear();
  }
}

bool Api::NeedToShow() const
{
  if (m_bookingCrossReferenceAwaitingForId.empty())
    return false;

  return NeedToShowImpl(eye::Eye::Instance().GetInfo());
}

std::string Api::GetCrossReferenceLinkAfterBooking() const
{
  auto const eyeInfo = eye::Eye::Instance().GetInfo();

  if (m_bookingCrossReferenceAwaitingForId.empty() || !NeedToShowImpl(eyeInfo))
    return "";

  return MakeCityGalleryUrl(m_baseUrl, m_bookingCrossReferenceAwaitingForId,
                            languages::GetCurrentNorm());
}

void Api::GetCrossReferenceCityGallery(std::string const & osmId,
                                       CityGalleryCallback const & cb) const
{
  GetCrossReferenceCityGalleryImpl(m_baseUrl, osmId, cb);
}

void Api::GetCrossReferenceCityGallery(m2::PointD const & point,
                                       CityGalleryCallback const & cb) const
{
  CHECK(m_delegate, ());

  GetCrossReferenceCityGalleryImpl(m_baseUrl, m_delegate->GetCityOsmId(point), cb);
}

void Api::OnMapObjectEvent(eye::MapObject const & mapObject)
{
  CHECK(!mapObject.GetEvents().empty(), ());

  auto const typeIt = std::find(kSupportedBookingTypes.cbegin(), kSupportedBookingTypes.cend(),
                                mapObject.GetBestType());

  if (typeIt == kSupportedBookingTypes.cend())
    return;

  m2::PointD pos;
  bool found = false;
  switch (mapObject.GetEvents().back().m_type)
  {
  case eye::MapObject::Event::Type::BookingBook:
  case eye::MapObject::Event::Type::BookingMore:
  case eye::MapObject::Event::Type::BookingReviews:
  case eye::MapObject::Event::Type::BookingDetails:
  {
    pos = mapObject.GetPos();
    found = true;
  }
  default: /* do nothing */;
  }

  auto const osmId = found ? m_delegate->GetCityOsmId(pos) : "";

  if (!osmId.empty())
    settings::Set("BookingCrossReferenceAwaitingForId", osmId);
}
}  // namespace cross_reference
