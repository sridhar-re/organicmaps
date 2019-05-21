#pragma once

#include "metrics/eye.hpp"

#include "geometry/point2d.hpp"

#include <functional>
#include <memory>
#include <string>

namespace cross_reference
{
struct Author
{
  std::string m_id;
  std::string m_name;
};
struct LuxCategory
{
  std::string m_name;
  std::string m_color;
};

struct CityGalleryItem
{
  std::string m_name;
  std::string m_url;
  std::string m_imageUrl;
  std::string m_access;
  std::string m_tier;
  Author m_author;
  LuxCategory m_luxCategory;
};

using CityGallery = std::vector<CityGalleryItem>;

class WebApi
{
public:
  static bool GetCityGalleryByOsmId(std::string const & baseUrl, std::string const & osmId,
                                    std::string const & lang, std::string & result);
};

using CityGalleryCallback = std::function<void(CityGallery const & gallery)>;

class Api : public eye::Subscriber
{
public:
  class Delegate
  {
  public:
    virtual ~Delegate() = default;

    virtual std::string GetCityOsmId(m2::PointD const & point) = 0;
  };

  explicit Api(std::string const & baseUrl = "https://routes.maps.me/gallery/v1/city/");

  void SetDelegate(std::unique_ptr<Delegate> delegate);
  void OnEnterForeground();
  bool NeedToShow() const;
  std::string GetCrossReferenceLinkAfterBooking() const;
  void GetCrossReferenceCityGallery(std::string const & osmId,
                                    CityGalleryCallback const & cb) const;
  void GetCrossReferenceCityGallery(m2::PointD const & point, CityGalleryCallback const & cb) const;

  // eye::Subscriber overrides:
  void OnMapObjectEvent(eye::MapObject const & poi) override;

private:
  std::unique_ptr<Delegate> m_delegate;

  std::string m_baseUrl;
  std::string m_bookingCrossReferenceAwaitingForId;
};
}  // namespace cross_reference
