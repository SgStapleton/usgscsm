#include "UsgsAstroFrameSensorModel.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#include <json.hpp>

#include <Error.h>
#include <Version.h>

using json = nlohmann::json;
using namespace std;

// Declaration of static variables
const std::string UsgsAstroFrameSensorModel::_SENSOR_MODEL_NAME
                                      = "USGS_ASTRO_FRAME_SENSOR_MODEL";
const int UsgsAstroFrameSensorModel::NUM_PARAMETERS = 7;
const std::string UsgsAstroFrameSensorModel::m_parameterName[] = {
  "X Sensor Position (m)",  // 0
  "Y Sensor Position (m)",  // 1
  "Z Sensor Position (m)",  // 2
  "w",                      // 3
  "v1",                     // 4
  "v2",                     // 5
  "v3"                      // 6
};

const int         UsgsAstroFrameSensorModel::_NUM_STATE_KEYWORDS = 32;

UsgsAstroFrameSensorModel::UsgsAstroFrameSensorModel() {
    reset();
}

void UsgsAstroFrameSensorModel::reset() {
    m_modelName = _SENSOR_MODEL_NAME;
    m_majorAxis = 0.0;
    m_minorAxis = 0.0;
    m_focalLength = 0.0;
    m_startingDetectorSample = 0.0;
    m_startingDetectorLine = 0.0;
    m_targetName = "";
    m_ifov = 0;
    m_instrumentID = "";
    m_focalLengthEpsilon = 0.0;
    m_linePp = 0.0;
    m_samplePp = 0.0;
    m_originalHalfLines = 0.0;
    m_spacecraftName = "";
    m_pixelPitch = 0.0;
    m_ephemerisTime = 0.0;
    m_originalHalfSamples = 0.0;
    m_nLines = 0;
    m_nSamples = 0;

    m_currentParameterValue = std::vector<double>(NUM_PARAMETERS, 0.0);
    m_currentParameterCovariance = std::vector<double>(NUM_PARAMETERS*NUM_PARAMETERS,0.0);
    m_noAdjustments = std::vector<double>(NUM_PARAMETERS,0.0);
    m_ccdCenter = std::vector<double>(2, 0.0);
    m_spacecraftVelocity = std::vector<double>(3, 0.0);
    m_sunPosition = std::vector<double>(3, 0.0);
    m_odtX = std::vector<double>(10, 0.0);
    m_odtY = std::vector<double>(10, 0.0);
    m_transX = std::vector<double>(3, 0.0);
    m_transY = std::vector<double>(3, 0.0);
    m_iTransS = std::vector<double>(3, 0.0);
    m_iTransL = std::vector<double>(3, 0.0);
    m_boresight = std::vector<double>(3, 0.0);
    m_parameterType = std::vector<csm::param::Type>(NUM_PARAMETERS, csm::param::REAL);
}


UsgsAstroFrameSensorModel::~UsgsAstroFrameSensorModel() {}

/**
 * @brief UsgsAstroFrameSensorModel::groundToImage
 * @param groundPt
 * @param desiredPrecision
 * @param achievedPrecision
 * @param warnings
 * @return Returns <line, sample> coordinate in the image corresponding to the ground point
 * without bundle adjustment correction.
 */
csm::ImageCoord UsgsAstroFrameSensorModel::groundToImage(const csm::EcefCoord &groundPt,
                              double desiredPrecision,
                              double *achievedPrecision,
                              csm::WarningList *warnings) const {

  return groundToImage(groundPt, m_noAdjustments, desiredPrecision, achievedPrecision, warnings);
}


/**
 * @brief UsgsAstroFrameSensorModel::groundToImage
 * @param groundPt
 * @param adjustments
 * @param desired_precision
 * @param achieved_precision
 * @param warnings
 * @return Returns <line,sample> coordinate in the image corresponding to the ground point.
 * This function applies bundle adjustments to the final value.
 */
csm::ImageCoord UsgsAstroFrameSensorModel::groundToImage(
    const csm::EcefCoord&      groundPt,
    const std::vector<double>& adjustments,
    double                     desired_precision,
    double*                    achieved_precision,
    csm::WarningList*          warnings ) const {

  double x = groundPt.x;
  double y = groundPt.y;
  double z = groundPt.z;

  double xo = x - getValue(0,adjustments);
  double yo = y - getValue(1,adjustments);
  double zo = z - getValue(2,adjustments);

  double f;
  f = m_focalLength;

  // Camera rotation matrix
  double m[3][3];
  calcRotationMatrix(m,adjustments);

  // Sensor position
  double undistortedx, undistortedy, denom;
  denom = m[0][2] * xo + m[1][2] * yo + m[2][2] * zo;
  undistortedx = (f * (m[0][0] * xo + m[1][0] * yo + m[2][0] * zo)/denom) + m_samplePp;  //m_samplePp like this assumes mm
  undistortedy = (f * (m[0][1] * xo + m[1][1] * yo + m[2][1] * zo)/denom) + m_linePp;

  // Apply the distortion to the line/sample location and then convert back to line/sample
  double distortedx, distortedy;
  distortionFunction(undistortedx, undistortedy, distortedx, distortedy);


  // Convert distorted mm into line/sample
  double sample, line;
  sample = m_iTransS[0] + m_iTransS[1] * distortedx + m_iTransS[2] * distortedy + m_ccdCenter[1];
  line =   m_iTransL[0] + m_iTransL[1] * distortedx + m_iTransL[2] * distortedy + m_ccdCenter[0];

  return csm::ImageCoord(line, sample);
}


csm::ImageCoordCovar UsgsAstroFrameSensorModel::groundToImage(const csm::EcefCoordCovar &groundPt,
                                   double desiredPrecision,
                                   double *achievedPrecision,
                                   csm::WarningList *warnings) const {

    throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
      "Unsupported function",
      "UsgsAstroFrameSensorModel::groundToImage");
}


csm::EcefCoord UsgsAstroFrameSensorModel::imageToGround(const csm::ImageCoord &imagePt,
                                                 double height,
                                                 double desiredPrecision,
                                                 double *achievedPrecision,
                                                 csm::WarningList *warnings) const {

  double sample = imagePt.samp;
  double line = imagePt.line;

  // Here is where we should be able to apply an adjustment to opk
  double m[3][3];
  calcRotationMatrix(m);

  // Apply the principal point offset, assuming the pp is given in pixels
  double xl, yl, zl, lo, so;
  lo = line - m_linePp;
  so = sample - m_samplePp;

  // Convert from the pixel space into the metric space
  double line_center, sample_center, x_camera, y_camera;
  line_center = m_ccdCenter[0];
  sample_center = m_ccdCenter[1];
  y_camera = m_transY[0] + m_transY[1] * (lo - line_center) + m_transY[2] * (so - sample_center);
  x_camera = m_transX[0] + m_transX[1] * (lo - line_center) + m_transX[2] * (so - sample_center);

  // Apply the distortion model (remove distortion)
  double undistorted_cameraX, undistorted_cameraY = 0.0;

  setFocalPlane(x_camera, y_camera, undistorted_cameraX, undistorted_cameraY);

  // Now back from distorted mm to pixels
  double udx, udy; //distorted line and sample
  udx = undistorted_cameraX;
  udy = undistorted_cameraY;

  xl = m[0][0] * udx + m[0][1] * udy - m[0][2] * -m_focalLength;
  yl = m[1][0] * udx + m[1][1] * udy - m[1][2] * -m_focalLength;
  zl = m[2][0] * udx + m[2][1] * udy - m[2][2] * -m_focalLength;

  double x, y, z;
  double xc, yc, zc;
  xc = m_currentParameterValue[0];
  yc = m_currentParameterValue[1];
  zc = m_currentParameterValue[2];

  // Intersect with some height about the ellipsoid.

  losEllipsoidIntersect(height, xc, yc, zc, xl, yl, zl, x, y, z);

  return csm::EcefCoord(x, y, z);
}


csm::EcefCoordCovar UsgsAstroFrameSensorModel::imageToGround(const csm::ImageCoordCovar &imagePt, double height,
                                  double heightVariance, double desiredPrecision,
                                  double *achievedPrecision,
                                  csm::WarningList *warnings) const {
    throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
      "Unsupported function",
      "UsgsAstroFrameSensorModel::imageToGround");
}


csm::EcefLocus UsgsAstroFrameSensorModel::imageToProximateImagingLocus(const csm::ImageCoord &imagePt,
                                                                const csm::EcefCoord &groundPt,
                                                                double desiredPrecision,
                                                                double *achievedPrecision,
                                                                csm::WarningList *warnings) const {
  // Ignore the ground point?
  return imageToRemoteImagingLocus(imagePt);
}


csm::EcefLocus UsgsAstroFrameSensorModel::imageToRemoteImagingLocus(const csm::ImageCoord &imagePt,
                                                             double desiredPrecision,
                                                             double *achievedPrecision,
                                                             csm::WarningList *warnings) const {
  // Find the line,sample on the focal plane (mm)
  // CSM center = 0.5, MDIS IK center = 1.0
  double row = imagePt.line - m_ccdCenter[0];
  double col = imagePt.samp - m_ccdCenter[1];
  double focalPlaneX = m_transX[0] + m_transX[1] * row + m_transX[2] * col;
  double focalPlaneY = m_transY[0] + m_transY[1] * row + m_transY[2] * col;

  // Distort
  double undistortedFocalPlaneX = focalPlaneX;
  double undistortedFocalPlaneY = focalPlaneY;

  setFocalPlane(focalPlaneX, focalPlaneY, undistortedFocalPlaneX, undistortedFocalPlaneY);

  // Get rotation matrix and transform to a body-fixed frame
  double m[3][3];
  calcRotationMatrix(m);
  std::vector<double> lookC { undistortedFocalPlaneX, undistortedFocalPlaneY, m_focalLength };
  std::vector<double> lookB {
    m[0][0] * lookC[0] + m[0][1] * lookC[1] + m[0][2] * lookC[2],
    m[1][0] * lookC[0] + m[1][1] * lookC[1] + m[1][2] * lookC[2],
    m[2][0] * lookC[0] + m[2][1] * lookC[1] + m[2][2] * lookC[2]
  };

  // Get unit vector
  double mag = sqrt(lookB[0] * lookB[0] + lookB[1] * lookB[1] + lookB[2] * lookB[2]);
  std::vector<double> lookBUnit {
    lookB[0] / mag,
    lookB[1] / mag,
    lookB[2] / mag
  };

  return csm::EcefLocus(m_currentParameterValue[0], m_currentParameterValue[1], m_currentParameterValue[2],
      lookBUnit[0], lookBUnit[1], lookBUnit[2]);
}


csm::ImageCoord UsgsAstroFrameSensorModel::getImageStart() const {

  csm::ImageCoord start;
  start.samp = m_startingDetectorSample;
  start.line = m_startingDetectorLine;
  return start;
}


csm::ImageVector UsgsAstroFrameSensorModel::getImageSize() const {

  csm::ImageVector size;
  size.line = m_nLines;
  size.samp = m_nSamples;
  return size;
}


std::pair<csm::ImageCoord, csm::ImageCoord> UsgsAstroFrameSensorModel::getValidImageRange() const {
    csm::ImageCoord min_pt(m_startingDetectorLine, m_startingDetectorSample);
    csm::ImageCoord max_pt(m_nLines, m_nSamples);
    return std::pair<csm::ImageCoord, csm::ImageCoord>(min_pt, max_pt);
}


std::pair<double, double> UsgsAstroFrameSensorModel::getValidHeightRange() const {
    return std::pair<double, double>(m_minElevation, m_maxElevation);
}


csm::EcefVector UsgsAstroFrameSensorModel::getIlluminationDirection(const csm::EcefCoord &groundPt) const {
  // ground (body-fixed) - sun (body-fixed) gives us the illumination direction.
  return csm::EcefVector {
    groundPt.x - m_sunPosition[0],
    groundPt.y - m_sunPosition[1],
    groundPt.z - m_sunPosition[2]
  };
}


double UsgsAstroFrameSensorModel::getImageTime(const csm::ImageCoord &imagePt) const {

  // check if the image point is in range
  if (imagePt.samp >= m_startingDetectorSample &&
      imagePt.samp <= (m_startingDetectorSample + m_nSamples) &&
      imagePt.line >= m_startingDetectorSample &&
      imagePt.line <= (m_startingDetectorLine + m_nLines)) {
    return m_ephemerisTime;
  }
  else {
    throw csm::Error(csm::Error::BOUNDS,
                     "Image Coordinate out of Bounds",
                     "UsgsAstroFrameSensorModel::getImageTime");
  }
}


csm::EcefCoord UsgsAstroFrameSensorModel::getSensorPosition(const csm::ImageCoord &imagePt) const {

  // check if the image point is in range
  if (imagePt.samp >= m_startingDetectorSample &&
      imagePt.samp <= (m_startingDetectorSample + m_nSamples) &&
      imagePt.line >= m_startingDetectorSample &&
      imagePt.line <= (m_startingDetectorLine + m_nLines)) {
    csm::EcefCoord sensorPosition;
    sensorPosition.x = m_currentParameterValue[0];
    sensorPosition.y = m_currentParameterValue[1];
    sensorPosition.z = m_currentParameterValue[2];

    return sensorPosition;
  }
  else {
    throw csm::Error(csm::Error::BOUNDS,
                     "Image Coordinate out of Bounds",
                     "UsgsAstroFrameSensorModel::getSensorPosition");
  }
}


csm::EcefCoord UsgsAstroFrameSensorModel::getSensorPosition(double time) const {
    if (time == m_ephemerisTime){
        csm::EcefCoord sensorPosition;
        sensorPosition.x = m_currentParameterValue[0];
        sensorPosition.y = m_currentParameterValue[1];
        sensorPosition.z = m_currentParameterValue[2];

        return sensorPosition;
    } else {
        std::string aMessage = "Valid image time is %d", m_ephemerisTime;
        throw csm::Error(csm::Error::BOUNDS,
                         aMessage,
                         "UsgsAstroFrameSensorModel::getSensorPosition");
    }
}


csm::EcefVector UsgsAstroFrameSensorModel::getSensorVelocity(const csm::ImageCoord &imagePt) const {
  // Make sure the passed coordinate is with the image dimensions.
  if (imagePt.samp < 0.0 || imagePt.samp > m_nSamples ||
      imagePt.line < 0.0 || imagePt.line > m_nLines) {
    throw csm::Error(csm::Error::BOUNDS, "Image coordinate out of bounds.",
                     "UsgsAstroFrameSensorModel::getSensorVelocity");
  }

  // Since this is a frame, just return the sensor velocity the ISD gave us.
  return csm::EcefVector {
    m_spacecraftVelocity[0],
    m_spacecraftVelocity[1],
    m_spacecraftVelocity[2]
  };
}


csm::EcefVector UsgsAstroFrameSensorModel::getSensorVelocity(double time) const {
    if (time == m_ephemerisTime){
        return csm::EcefVector {
          m_spacecraftVelocity[0],
          m_spacecraftVelocity[1],
          m_spacecraftVelocity[2]
        };
    } else {
        std::string aMessage = "Valid image time is %d", m_ephemerisTime;
        throw csm::Error(csm::Error::BOUNDS,
                         aMessage,
                         "UsgsAstroFrameSensorModel::getSensorVelocity");
    }
}


csm::RasterGM::SensorPartials UsgsAstroFrameSensorModel::computeSensorPartials(int index,
                                           const csm::EcefCoord &groundPt,
                                           double desiredPrecision,
                                           double *achievedPrecision,
                                           csm::WarningList *warnings) const {

    csm::ImageCoord img_pt = groundToImage(groundPt, desiredPrecision, achievedPrecision);

    return computeSensorPartials(index, img_pt, groundPt, desiredPrecision, achievedPrecision);
}


/**
 * @brief UsgsAstroFrameSensorModel::computeSensorPartials
 * @param index
 * @param imagePt
 * @param groundPt
 * @param desiredPrecision
 * @param achievedPrecision
 * @param warnings
 * @return The partial derivatives in the line,sample directions.
 *
 * Research:  We should investigate using a central difference scheme to approximate
 * the partials.  It is more accurate, but it might be costlier calculation-wise.
 *
 */
csm::RasterGM::SensorPartials UsgsAstroFrameSensorModel::computeSensorPartials(int index,
                                          const csm::ImageCoord &imagePt,
                                          const csm::EcefCoord &groundPt,
                                          double desiredPrecision,
                                          double *achievedPrecision,
                                          csm::WarningList *warnings) const {


  const double delta = 1.0;
  // Update the parameter
  std::vector<double>adj(NUM_PARAMETERS, 0.0);
  adj[index] = delta;

  csm::ImageCoord imagePt1 = groundToImage(groundPt,adj,desiredPrecision,achievedPrecision);

  csm::RasterGM::SensorPartials partials;

  partials.first = (imagePt1.line - imagePt.line)/delta;
  partials.second = (imagePt1.samp - imagePt.samp)/delta;

  return partials;

}

std::vector<csm::RasterGM::SensorPartials> UsgsAstroFrameSensorModel::computeAllSensorPartials(
    const csm::ImageCoord& imagePt,
    const csm::EcefCoord& groundPt,
    csm::param::Set pset,
    double desiredPrecision,
    double *achievedPrecision,
    csm::WarningList *warnings) const {
  std::vector<int> indices = getParameterSetIndices(pset);
  size_t num = indices.size();
  std::vector<csm::RasterGM::SensorPartials> partials;
  for (int index = 0;index < num;index++){
    partials.push_back(computeSensorPartials(
        indices[index],
        imagePt, groundPt,
        desiredPrecision, achievedPrecision, warnings));
  }
  return partials;
}

std::vector<csm::RasterGM::SensorPartials> UsgsAstroFrameSensorModel::computeAllSensorPartials(
    const csm::EcefCoord& groundPt,
    csm::param::Set pset,
    double desiredPrecision,
    double *achievedPrecision,
    csm::WarningList *warnings) const {
  csm::ImageCoord imagePt = groundToImage(groundPt,
                                          desiredPrecision, achievedPrecision, warnings);
  return computeAllSensorPartials(imagePt, groundPt,
                                  pset, desiredPrecision, achievedPrecision, warnings);
    }

std::vector<double> UsgsAstroFrameSensorModel::computeGroundPartials(const csm::EcefCoord
                                                                     &groundPt) const {
  // Partials of line, sample wrt X, Y, Z
  // Uses collinearity equations
  std::vector<double> partials(6, 0.0);

  double m[3][3];
  calcRotationMatrix(m, m_noAdjustments);

  double xo, yo, zo;
  xo = groundPt.x - m_currentParameterValue[0];
  yo = groundPt.y - m_currentParameterValue[1];
  zo = groundPt.z - m_currentParameterValue[2];

  double u, v, w;
  u = m[0][0] * xo + m[0][1] * yo + m[0][2] * zo;
  v = m[1][0] * xo + m[1][1] * yo + m[1][2] * zo;
  w = m[2][0] * xo + m[2][1] * yo + m[2][2] * zo;

  double fdw, udw, vdw;
  fdw = m_focalLength / w;
  udw = u / w;
  vdw = v / w;

  double upx, vpx, wpx;
  upx = m[0][0];
  vpx = m[1][0];
  wpx = m[2][0];
  partials[0] = -fdw * ( upx - udw * wpx );
  partials[3] = -fdw * ( vpx - vdw * wpx );

  double upy, vpy, wpy;
  upy = m[0][1];
  vpy = m[1][1];
  wpy = m[2][1];
  partials[1] = -fdw * ( upy - udw * wpy );
  partials[4] = -fdw * ( vpy - vdw * wpy );

  double upz, vpz, wpz;
  upz = m[0][2];
  vpz = m[1][2];
  wpz = m[2][2];
  partials[2] = -fdw * ( upz - udw * wpz );
  partials[5] = -fdw * ( vpz - vdw * wpz );

  return partials;
}


const csm::CorrelationModel& UsgsAstroFrameSensorModel::getCorrelationModel() const {
  return _no_corr_model;
}


std::vector<double> UsgsAstroFrameSensorModel::getUnmodeledCrossCovariance(const csm::ImageCoord &pt1,
                                                const csm::ImageCoord &pt2) const {

  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getUnmodeledCrossCovariance");
}


csm::Version UsgsAstroFrameSensorModel::getVersion() const {
  return csm::Version(0,1,0);
}


std::string UsgsAstroFrameSensorModel::getModelName() const {
  return _SENSOR_MODEL_NAME;
}


std::string UsgsAstroFrameSensorModel::getPedigree() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getPedigree");
}


std::string UsgsAstroFrameSensorModel::getImageIdentifier() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getImageIdentifier");
}


void UsgsAstroFrameSensorModel::setImageIdentifier(const std::string& imageId,
                                            csm::WarningList* warnings) {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::setImageIdentifier");
}


std::string UsgsAstroFrameSensorModel::getSensorIdentifier() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getSensorIdentifier");
}


std::string UsgsAstroFrameSensorModel::getPlatformIdentifier() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getPlatformIdentifier");
}


std::string UsgsAstroFrameSensorModel::getCollectionIdentifier() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getCollectionIdentifier");
}


std::string UsgsAstroFrameSensorModel::getTrajectoryIdentifier() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getTrajectoryIdentifier");
}


std::string UsgsAstroFrameSensorModel::getSensorType() const {
    return CSM_SENSOR_TYPE_EO;
}


std::string UsgsAstroFrameSensorModel::getSensorMode() const {
    return CSM_SENSOR_MODE_FRAME;
}


std::string UsgsAstroFrameSensorModel::getReferenceDateAndTime() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getReferenceDateAndTime");
}


std::string UsgsAstroFrameSensorModel::getModelState() const {
    json state = {
      {"m_modelName", _SENSOR_MODEL_NAME},
      {"m_focalLength" , m_focalLength},
      {"m_iTransS", {m_iTransS[0], m_iTransS[1], m_iTransS[2]}},
      {"m_iTransL", {m_iTransL[0], m_iTransL[1], m_iTransL[2]}},
      {"m_boresight", {m_boresight[0], m_boresight[1], m_boresight[2]}},
      {"m_transX", {m_transX[0], m_transX[1], m_transX[2]}},
      {"m_transY", {m_transY[0], m_transY[1], m_transY[2]}},
      {"m_iTransS", {m_iTransS[0], m_iTransS[1], m_iTransS[2]}},
      {"m_iTransL", {m_iTransL[0], m_iTransL[1], m_iTransL[2]}},
      {"m_majorAxis", m_majorAxis},
      {"m_minorAxis", m_minorAxis},
      {"m_spacecraftVelocity", {m_spacecraftVelocity[0], m_spacecraftVelocity[1], m_spacecraftVelocity[2]}},
      {"m_sunPosition", {m_sunPosition[0], m_sunPosition[1], m_sunPosition[2]}},
      {"m_startingDetectorSample", m_startingDetectorSample},
      {"m_startingDetectorLine", m_startingDetectorLine},
      {"m_targetName", m_targetName},
      {"m_ifov", m_ifov},
      {"m_instrumentID", m_instrumentID},
      {"m_focalLengthEpsilon", m_focalLengthEpsilon},
      {"m_ccdCenter", {m_ccdCenter[0], m_ccdCenter[1]}},
      {"m_linePp", m_linePp},
      {"m_samplePp", m_samplePp},
      {"m_minElevation", m_minElevation},
      {"m_maxElevation", m_maxElevation},
      {"m_odtX", {m_odtX[0], m_odtX[1], m_odtX[2], m_odtX[3], m_odtX[4],
                  m_odtX[5], m_odtX[6], m_odtX[7], m_odtX[8], m_odtX[9]}},
      {"m_odtY", {m_odtY[0], m_odtY[1], m_odtY[2], m_odtY[3], m_odtY[4],
                  m_odtY[5], m_odtY[6], m_odtY[7], m_odtY[8], m_odtY[9]}},
      {"m_originalHalfLines", m_originalHalfLines},
      {"m_originalHalfSamples", m_originalHalfSamples},
      {"m_spacecraftName", m_spacecraftName},
      {"m_pixelPitch", m_pixelPitch},
      {"m_ephemerisTime", m_ephemerisTime},
      {"m_nLines", m_nLines},
      {"m_nSamples", m_nSamples},
      {"m_currentParameterValue", {m_currentParameterValue[0], m_currentParameterValue[1],
                                   m_currentParameterValue[2], m_currentParameterValue[3],
                                   m_currentParameterValue[4], m_currentParameterValue[5],
                                   m_currentParameterValue[6]}},
      {"m_currentParameterCovariance", m_currentParameterCovariance}
    };

    return state.dump();
}

bool UsgsAstroFrameSensorModel::isValidModelState(const std::string& stringState, csm::WarningList *warnings) {
  std::vector<std::string> requiredKeywords = {
    "m_modelName",
    "m_majorAxis",
    "m_minorAxis",
    "m_focalLength",
    "m_startingDetectorSample",
    "m_startingDetectorLine",
    "m_focalLengthEpsilon",
    "m_nLines",
    "m_nSamples",
    "m_currentParameterValue",
    "m_ccdCenter",
    "m_spacecraftVelocity",
    "m_sunPosition",
    "m_odtX",
    "m_odtY",
    "m_transX",
    "m_transY",
    "m_iTransS",
    "m_iTransL"
  };

  json jsonState = json::parse(stringState);
  std::vector<std::string> missingKeywords;

  for (auto &key : requiredKeywords) {
    if (jsonState.find(key) == jsonState.end()) {
      missingKeywords.push_back(key);
    }
  }

  if (!missingKeywords.empty()) {
    std::ostringstream oss;
    std::copy(missingKeywords.begin(), missingKeywords.end(), std::ostream_iterator<std::string>(oss, " "));
    warnings->push_back(csm::Warning(
      csm::Warning::DATA_NOT_AVAILABLE,
      "State has missing keywrods: " + oss.str(),
      "UsgsAstroFrameSensorModel::isValidModelState"
    ));
  }

  std::string modelName = jsonState.value<std::string>("m_modelName", "");

  if (modelName != _SENSOR_MODEL_NAME) {
    warnings->push_back(csm::Warning(
      csm::Warning::DATA_NOT_AVAILABLE,
      "Incorrect model name in state, expected " + _SENSOR_MODEL_NAME + " but got " + modelName,
      "UsgsAstroFrameSensorModel::isValidModelState"
    ));
  }

  return modelName == _SENSOR_MODEL_NAME && missingKeywords.empty();
}


bool UsgsAstroFrameSensorModel::isValidIsd(const std::string& Isd, csm::WarningList *warnings) {
  // no obvious clean way to truely validate ISD with it's nested structure,
  // or rather, it would be a pain to maintain, so just check if
  // we can get a valid state from ISD. Once ISD schema is 100% clear
  // we can change this.
   try {
     std::string state = constructStateFromIsd(Isd, warnings);
     return isValidModelState(state, warnings);
   }
   catch(...) {
     return false;
   }
}


void UsgsAstroFrameSensorModel::replaceModelState(const std::string& stringState) {

    json state = json::parse(stringState);

    // The json library's .at() will except if key is missing
    try {
        m_modelName = state.at("m_modelName").get<std::string>();
        m_majorAxis = state.at("m_majorAxis").get<double>();
        m_minorAxis = state.at("m_minorAxis").get<double>();
        m_focalLength = state.at("m_focalLength").get<double>();
        m_startingDetectorSample = state.at("m_startingDetectorSample").get<double>();
        m_startingDetectorLine = state.at("m_startingDetectorLine").get<double>();
        m_focalLengthEpsilon = state.at("m_focalLengthEpsilon").get<double>();
        m_nLines = state.at("m_nLines").get<int>();
        m_nSamples = state.at("m_nSamples").get<int>();
        m_currentParameterValue = state.at("m_currentParameterValue").get<std::vector<double>>();
        m_ccdCenter = state.at("m_ccdCenter").get<std::vector<double>>();
        m_spacecraftVelocity = state.at("m_spacecraftVelocity").get<std::vector<double>>();
        m_sunPosition = state.at("m_sunPosition").get<std::vector<double>>();
        m_odtX = state.at("m_odtX").get<std::vector<double>>();
        m_odtY = state.at("m_odtY").get<std::vector<double>>();
        m_transX = state.at("m_transX").get<std::vector<double>>();
        m_transY = state.at("m_transY").get<std::vector<double>>();
        m_iTransS = state.at("m_iTransS").get<std::vector<double>>();
        m_iTransL = state.at("m_iTransL").get<std::vector<double>>();

        // Leaving unused params commented out
        // m_targetName = state.at("m_targetName").get<std::string>();
        // m_ifov = state.at("m_ifov").get<double>();
        // m_instrumentID = state.at("m_instrumentID").get<std::string>();
        // m_currentParameterCovariance = state.at("m_currentParameterCovariance").get<std::vector<double>>();
        // m_noAdjustments = state.at("m_noAdjustments").get<std::vector<double>>();
        // m_linePp = state.at("m_linePp").get<double>();
        // m_samplePp = state.at("m_samplePp").get<double>();
        // m_originalHalfLines = state.at("m_originalHalfLines").get<double>();
        // m_spacecraftName = state.at("m_spacecraftName").get<std::string>();
        // m_pixelPitch = state.at("m_pixelPitch").get<double>();
        // m_ephemerisTime = state.at("m_ephemerisTime").get<double>();
        // m_originalHalfSamples = state.at("m_originalHalfSamples").get<double>();
        // m_boresight = state.at("m_boresight").get<std::vector<double>>();

        // Cast int vector to csm::param::Type vector by simply copying it
        // std::vector<int> paramType = state.at("m_parameterType").get<std::vector<int>>();
        // m_parameterType = std::vector<csm::param::Type>();
        // for(auto &t : paramType){
           // paramType.push_back(static_cast<csm::param::Type>(t));
        // }
    }
    catch(std::out_of_range& e) {
      throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                       "State keywords required to generate sensor model missing: " + std::string(e.what()) + "\nUsing model string: " + stringState,
                       "UsgsAstroFrameSensorModel::replaceModelState");
    }
}


std::string UsgsAstroFrameSensorModel::constructStateFromIsd(const std::string& jsonIsd, csm::WarningList* warnings) {
    auto metric_conversion = [](double val, std::string from, std::string to="m") {
        json typemap = {
          {"m", 0},
          {"km", 3}
        };

        // everything to lowercase
        std::transform(from.begin(), from.end(), from.begin(), ::tolower);
        std::transform(to.begin(), to.end(), to.begin(), ::tolower);
        return val*pow(10, typemap[from].get<int>() - typemap[to].get<int>());
    };

    json isd = json::parse(jsonIsd);
    json state = {};


    try {
      state["m_modelName"] = isd.at("name_model");
      std::cerr << "Model Name Parsed!" << std::endl;

      state["m_startingDetectorSample"] = isd.at("starting_detector_sample");
      state["m_startingDetectorLine"] = isd.at("starting_detector_line");

      std::cerr << "Detector Starting Pixel Parsed!" << std::endl;

      // get focal length
      {
        json jayson = isd.at("focal_length_model");
        json focal_length = jayson.at("focal_length");
        json epsilon = jayson.at("focal_epsilon");

        state["m_focalLength"] = focal_length;
        state["m_focalLengthEpsilon"] = epsilon;

        std::cerr << "Focal Length Parsed!" << std::endl;
      }

      // get sensor_position
      {
        json jayson = isd.at("sensor_position");
        json positions = jayson.at("positions")[0];
        json velocities = jayson.at("velocities")[0];
        json unit = jayson.at("unit");

        unit = unit.get<std::string>();
        state["m_currentParameterValue"] = json();
        state["m_currentParameterValue"][0] = metric_conversion(positions[0].get<double>(), unit);
        state["m_currentParameterValue"][1] = metric_conversion(positions[1].get<double>(), unit);
        state["m_currentParameterValue"][2] = metric_conversion(positions[2].get<double>(), unit);
        state["m_spacecraftVelocity"] = velocities;

        std::cerr << "Sensor Location Parsed!" << std::endl;
      }

      // get sun_position
      // sun position is not strictly necessary, but is required for getIlluminationDirection.
      {
        json jayson = isd.at("sun_position");
        json positions = jayson.at("positions")[0];
        json unit = jayson.at("unit");

        unit = unit.get<std::string>();
        state["m_sunPosition"] = json();
        state["m_sunPosition"][0] = metric_conversion(positions[0].get<double>(), unit);
        state["m_sunPosition"][1] = metric_conversion(positions[1].get<double>(), unit);
        state["m_sunPosition"][2] = metric_conversion(positions[2].get<double>(), unit);

        std::cerr << "Sun Position Parsed!" << std::endl;
      }

      // get sensor_orientation quaternion
      {
        json jayson = isd.at("sensor_orientation");
        json quaternion = jayson.at("quaternions")[0];

        state["m_currentParameterValue"][3] = quaternion[0];
        state["m_currentParameterValue"][4] = quaternion[1];
        state["m_currentParameterValue"][5] = quaternion[2];
        state["m_currentParameterValue"][6] = quaternion[3];

        std::cerr << "Sensor Orientation Parsed!" << std::endl;
      }

      // get optical_distortion
      {
        json jayson = isd.at("optical_distortion");
        std::vector<double> xDistortion = jayson.at("transverse").at("x");
        std::vector<double> yDistortion = jayson.at("transverse").at("y");
        xDistortion.resize(10, 0.0);
        yDistortion.resize(10, 0.0);

        state["m_odtX"] = xDistortion;
        state["m_odtY"] = yDistortion;

        std::cerr << "Distortion Parsed!" << std::endl;
      }

      // get detector_center
      {
        json jayson = isd.at("detector_center");
        json sample = jayson.at("sample");
        json line = jayson.at("line");

        state["m_ccdCenter"][0] = line;
        state["m_ccdCenter"][1] = sample;

        std::cerr << "Detector Center Parsed!" << std::endl;
      }

      // get radii
      {
        json jayson = isd.at("radii");
        json semiminor = jayson.at("semiminor");
        json semimajor = jayson.at("semimajor");
        json unit = jayson.at("unit");

        unit = unit.get<std::string>();
        state["m_minorAxis"] = metric_conversion(semiminor.get<double>(), unit);
        state["m_majorAxis"] = metric_conversion(semimajor.get<double>(), unit);

        std::cerr << "Target Radii Parsed!" << std::endl;
      }

      // get reference_height
      {
        json reference_height = isd.at("reference_height");
        json maxheight = reference_height.at("maxheight");
        json minheight = reference_height.at("minheight");
        json unit = reference_height.at("unit");

        unit = unit.get<std::string>();
        state["m_minElevation"] = metric_conversion(minheight.get<double>(), unit);
        state["m_maxElevation"] = metric_conversion(maxheight.get<double>(), unit);

        std::cerr << "Reference Height Parsed!" << std::endl;
      }

      state["m_ephemerisTime"] = isd.at("center_ephemeris_time");
      state["m_nLines"] = isd.at("image_lines");
      state["m_nSamples"] = isd.at("image_samples");

      state["m_iTransL"] = isd.at("focal2pixel_lines");

      state["m_iTransS"] = isd.at("focal2pixel_samples");

      // We don't pass the pixel to focal plane transformation so invert the
      // focal plane to pixel transformation
      double determinant = state["m_iTransL"][1].get<double>() * state["m_iTransS"][2].get<double>() -
                           state["m_iTransL"][2].get<double>() * state["m_iTransS"][1].get<double>();

      state["m_transX"][1] =  state["m_iTransL"][1].get<double>() / determinant;
      state["m_transX"][2] = -state["m_iTransS"][1].get<double>() / determinant;
      state["m_transX"][0] = -(state["m_transX"][1].get<double>() * state["m_iTransL"][0].get<double>() +
                              state["m_transX"][2].get<double>() * state["m_iTransS"][0].get<double>());

      state["m_transY"][1] = -state["m_iTransL"][2].get<double>() / determinant;
      state["m_transY"][2] =  state["m_iTransS"][2].get<double>() / determinant;
      state["m_transY"][0] = -(state["m_transY"][1].get<double>() * state["m_iTransL"][0].get<double>() +
                               state["m_transY"][2].get<double>() * state["m_iTransS"][0].get<double>());

      std::cerr << "Focal To Pixel Transformation Parsed!" << std::endl;

    }
    catch(std::out_of_range& e) {
      throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                       "ISD missing necessary keywords to create sensor model: " + std::string(e.what()),
                       "UsgsAstroFrameSensorModel::constructStateFromIsd");
    }
    catch(...) {
      throw csm::Error(csm::Error::SENSOR_MODEL_NOT_CONSTRUCTIBLE,
                       "ISD is invalid for creating the sensor model.",
                       "UsgsAstroFrameSensorModel::constructStateFromIsd");
    }

    return state.dump();

}


csm::EcefCoord UsgsAstroFrameSensorModel::getReferencePoint() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getReferencePoint");
}


void UsgsAstroFrameSensorModel::setReferencePoint(const csm::EcefCoord &groundPt) {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::setReferencePoint");
}


int UsgsAstroFrameSensorModel::getNumParameters() const {
  return NUM_PARAMETERS;
}


std::string UsgsAstroFrameSensorModel::getParameterName(int index) const {
  return m_parameterName[index];
}


std::string UsgsAstroFrameSensorModel::getParameterUnits(int index) const {

  if (index < 3) {
    return "m";
  }
  else {
    return "radians";
  }
}


bool UsgsAstroFrameSensorModel::hasShareableParameters() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::hasShareableParameters");
}


bool UsgsAstroFrameSensorModel::isParameterShareable(int index) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::isParameterShareable");
}


csm::SharingCriteria UsgsAstroFrameSensorModel::getParameterSharingCriteria(int index) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getParameterSharingCriteria");
}


double UsgsAstroFrameSensorModel::getParameterValue(int index) const {

  return m_currentParameterValue[index];

}


void UsgsAstroFrameSensorModel::setParameterValue(int index, double value) {
  m_currentParameterValue[index] = value;
}


csm::param::Type UsgsAstroFrameSensorModel::getParameterType(int index) const {
  return m_parameterType[index];
}


void UsgsAstroFrameSensorModel::setParameterType(int index, csm::param::Type pType) {
    m_parameterType[index] = pType;
}


double UsgsAstroFrameSensorModel::getParameterCovariance(int index1, int index2) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getParameterCovariance");
}


void UsgsAstroFrameSensorModel::setParameterCovariance(int index1, int index2, double covariance) {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::setParameterCovariance");
}


int UsgsAstroFrameSensorModel::getNumGeometricCorrectionSwitches() const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getNumGeometricCorrectionSwitches");
}


std::string UsgsAstroFrameSensorModel::getGeometricCorrectionName(int index) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getGeometricCorrectionName");
}


void UsgsAstroFrameSensorModel::setGeometricCorrectionSwitch(int index,
                                                      bool value,
                                                      csm::param::Type pType) {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::setGeometricCorrectionSwitch");
}


bool UsgsAstroFrameSensorModel::getGeometricCorrectionSwitch(int index) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getGeometricCorrectionSwitch");
}


std::vector<double> UsgsAstroFrameSensorModel::getCrossCovarianceMatrix(
    const GeometricModel &comparisonModel,
    csm::param::Set pSet,
    const GeometricModelList &otherModels) const {
  throw csm::Error(csm::Error::UNSUPPORTED_FUNCTION,
                   "Unsupported function",
                   "UsgsAstroFrameSensorModel::getCrossCovarianceMatrix");
}


void UsgsAstroFrameSensorModel::calcRotationMatrix(
    double m[3][3]) const {
  // Trigonometric functions for rotation matrix
  double w = m_currentParameterValue[3];
  double x = m_currentParameterValue[4];
  double y = m_currentParameterValue[5];
  double z = m_currentParameterValue[6];

  m[0][0] = w*w + x*x - y*y - z*z;
  m[0][1] = 2 * (x*y - w*z);
  m[0][2] = 2 * (w*y + x*z);
  m[1][0] = 2 * (x*y + w*z);
  m[1][1] = w*w - x*x + y*y - z*z;
  m[1][2] = 2 * (y*z - w*x);
  m[2][0] = 2 * (x*z - w*y);
  m[2][1] = 2 * (w*x + y*z);
  m[2][2] = w*w - x*x - y*y + z*z;
}


void UsgsAstroFrameSensorModel::calcRotationMatrix(
  double m[3][3], const std::vector<double> &adjustments) const {
  // Trigonometric functions for rotation matrix
  double w = getValue(3, adjustments);
  double x = getValue(4, adjustments);
  double y = getValue(5, adjustments);
  double z = getValue(6, adjustments);

  m[0][0] = w*w + x*x - y*y - z*z;
  m[0][1] = 2 * (x*y - w*z);
  m[0][2] = 2 * (w*y + x*z);
  m[1][0] = 2 * (x*y + w*z);
  m[1][1] = w*w - x*x + y*y - z*z;
  m[1][2] = 2 * (y*z - w*x);
  m[2][0] = 2 * (x*z - w*y);
  m[2][1] = 2 * (w*x + y*z);
  m[2][2] = w*w - x*x - y*y + z*z;
}


void UsgsAstroFrameSensorModel::losEllipsoidIntersect(
      const double height,
      const double xc,
      const double yc,
      const double zc,
      const double xl,
      const double yl,
      const double zl,
      double&       x,
      double&       y,
      double&       z ) const
{
   // Helper function which computes the intersection of the image ray
   // with an expanded ellipsoid.  All vectors are in earth-centered-fixed
   // coordinate system with origin at the center of the earth.

   double ap, bp, k;
   ap = m_majorAxis + height;
   bp = m_minorAxis + height;
   k = ap * ap / (bp * bp);

   // Solve quadratic equation for scale factor
   // applied to image ray to compute ground point

   double at, bt, ct, quadTerm;
   at = xl * xl + yl * yl + k * zl * zl;
   bt = 2.0 * (xl * xc + yl * yc + k * zl * zc);
   ct = xc * xc + yc * yc + k * zc * zc - ap * ap;
   quadTerm = bt * bt - 4.0 * at * ct;

   // If quadTerm is negative, the image ray does not
   // intersect the ellipsoid. Setting the quadTerm to
   // zero means solving for a point on the ray nearest
   // the surface of the ellisoid.

   if ( 0.0 > quadTerm )
   {
      quadTerm = 0.0;
   }
   double scale;
   scale = (-bt - sqrt (quadTerm)) / (2.0 * at);
   // Compute ground point vector

   x = xc + scale * xl;
   y = yc + scale * yl;
   z = zc + scale * zl;
}


/**
 * @brief Compute undistorted focal plane x/y.
 *
 * Computes undistorted focal plane (x,y) coordinates given a distorted focal plane (x,y)
 * coordinate. The undistorted coordinates are solved for using the Newton-Raphson
 * method for root-finding if the distortionFunction method is invoked.
 *
 * @param dx distorted focal plane x in millimeters
 * @param dy distorted focal plane y in millimeters
 * @param undistortedX The undistorted x coordinate, in millimeters.
 * @param undistortedY The undistorted y coordinate, in millimeters.
 *
 * @return if the conversion was successful
 * @todo Review the tolerance and maximum iterations of the root-
 *       finding algorithm.
 * @todo Review the handling of non-convergence of the root-finding
 *       algorithm.
 * @todo Add error handling for near-zero determinant.
*/
bool UsgsAstroFrameSensorModel::setFocalPlane(double dx,double dy,
                                       double &undistortedX,
                                       double &undistortedY ) const {

  // Solve the distortion equation using the Newton-Raphson method.
  // Set the error tolerance to about one millionth of a NAC pixel.
  const double tol = 1.4E-5;

  // The maximum number of iterations of the Newton-Raphson method.
  const int maxTries = 60;

  double x;
  double y;
  double fx;
  double fy;
  double Jxx;
  double Jxy;
  double Jyx;
  double Jyy;

  // Initial guess at the root
  x = dx;
  y = dy;

  distortionFunction(x, y, fx, fy);


  for (int count = 1; ((fabs(fx) +fabs(fy)) > tol) && (count < maxTries); count++) {

    this->distortionFunction(x, y, fx, fy);

    fx = dx - fx;
    fy = dy - fy;

    distortionJacobian(x, y, Jxx, Jxy, Jyx, Jyy);

    double determinant = Jxx * Jyy - Jxy * Jyx;
    if (fabs(determinant) < 1E-6) {

      undistortedX = x;
      undistortedY = y;
      //
      // Near-zero determinant. Add error handling here.
      //
      //-- Just break out and return with no convergence
      return false;
    }

    x = x + (Jyy * fx - Jxy * fy) / determinant;
    y = y + (Jxx * fy - Jyx * fx) / determinant;
  }

  if ( (fabs(fx) + fabs(fy)) <= tol) {
    // The method converged to a root.
    undistortedX = x;
    undistortedY = y;
    return true;

  }
  else {
    // The method did not converge to a root within the maximum
    // number of iterations. Return with no distortion.
    undistortedX = dx;
    undistortedY = dy;
    return false;
  }
  return true;
}


/**
 * @description Jacobian of the distortion function. The Jacobian was computed
 * algebraically from the function described in the distortionFunction
 * method.
 *
 * @param x
 * @param y
 * @param Jxx  Partial_xx
 * @param Jxy  Partial_xy
 * @param Jyx  Partial_yx
 * @param Jyy  Partial_yy
 */
void UsgsAstroFrameSensorModel::distortionJacobian(double x, double y, double &Jxx, double &Jxy,
                                            double &Jyx, double &Jyy) const {

  double d_dx[10];
  d_dx[0] = 0;
  d_dx[1] = 1;
  d_dx[2] = 0;
  d_dx[3] = 2 * x;
  d_dx[4] = y;
  d_dx[5] = 0;
  d_dx[6] = 3 * x * x;
  d_dx[7] = 2 * x * y;
  d_dx[8] = y * y;
  d_dx[9] = 0;
  double d_dy[10];
  d_dy[0] = 0;
  d_dy[1] = 0;
  d_dy[2] = 1;
  d_dy[3] = 0;
  d_dy[4] = x;
  d_dy[5] = 2 * y;
  d_dy[6] = 0;
  d_dy[7] = x * x;
  d_dy[8] = 2 * x * y;
  d_dy[9] = 3 * y * y;

  Jxx = 0.0;
  Jxy = 0.0;
  Jyx = 0.0;
  Jyy = 0.0;

  for (int i = 0; i < 10; i++) {
    Jxx = Jxx + d_dx[i] * m_odtX[i];
    Jxy = Jxy + d_dy[i] * m_odtX[i];
    Jyx = Jyx + d_dx[i] * m_odtY[i];
    Jyy = Jyy + d_dy[i] * m_odtY[i];
  }
}



/**
 * @description Compute distorted focal plane (dx,dy) coordinate  given an undistorted focal
 * plane (ux,uy) coordinate. This describes the third order Taylor approximation to the
 * distortion model.
 *
 * @param ux Undistored x
 * @param uy Undistored y
 * @param dx Result distorted x
 * @param dy Result distorted y
 */
void UsgsAstroFrameSensorModel::distortionFunction(double ux, double uy, double &dx, double &dy) const {

  double f[10];
  f[0] = 1;
  f[1] = ux;
  f[2] = uy;
  f[3] = ux * ux;
  f[4] = ux * uy;
  f[5] = uy * uy;
  f[6] = ux * ux * ux;
  f[7] = ux * ux * uy;
  f[8] = ux * uy * uy;
  f[9] = uy * uy * uy;

  dx = 0.0;
  dy = 0.0;
  for (int i = 0; i < 10; i++) {
    dx = dx + f[i] * m_odtX[i];
    dy = dy + f[i] * m_odtY[i];
  }
}

/***** Helper Functions *****/

double UsgsAstroFrameSensorModel::getValue(
   int index,
   const std::vector<double> &adjustments) const
{
   return m_currentParameterValue[index] + adjustments[index];
}
