/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainAirmapQuery.h"
#include "TerrainTileManager.h"
#include "TerrainQueryTest.h"

#include "QGCApplication.h"
#include "QGCFileDownload.h"
#include "QGCLoggingCategory.h"

#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QGC_LOGGING_CATEGORY(TerrainAirmapQueryLog, "TerrainAirmapQueryLog")
QGC_LOGGING_CATEGORY(TerrainAirmapQueryVerboseLog, "TerrainAirmapQueryVerboseLog")

TerrainAirMapQuery::TerrainAirMapQuery(QObject* parent)
    : TerrainQueryInterface(parent)
{
    qCDebug(TerrainAirmapQueryVerboseLog) << "supportsSsl" << QSslSocket::supportsSsl() << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();
}

void TerrainAirMapQuery::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCoordinateHeights(coordinates);
        return;
    }

    QString points;
    for (const QGeoCoordinate& coord: coordinates) {
            points += QString::number(coord.latitude(), 'f', 10) + ","
                    + QString::number(coord.longitude(), 'f', 10) + ",";
    }
    points = points.mid(0, points.length() - 1); // remove the last ',' from string

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("points"), points);

    _queryMode = QueryModeCoordinates;
    _sendQuery(QString() /* path */, query);
}

void TerrainAirMapQuery::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestPathHeights(fromCoord, toCoord);
        return;
    }

    QString points;
    points += QString::number(fromCoord.latitude(), 'f', 10) + ","
            + QString::number(fromCoord.longitude(), 'f', 10) + ",";
    points += QString::number(toCoord.latitude(), 'f', 10) + ","
            + QString::number(toCoord.longitude(), 'f', 10);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("points"), points);

    _queryMode = QueryModePath;
    _sendQuery(QStringLiteral("/path"), query);
}

void TerrainAirMapQuery::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCarpetHeights(swCoord, neCoord, statsOnly);
        return;
    }

    QString points;
    points += QString::number(swCoord.latitude(), 'f', 10) + ","
            + QString::number(swCoord.longitude(), 'f', 10) + ",";
    points += QString::number(neCoord.latitude(), 'f', 10) + ","
            + QString::number(neCoord.longitude(), 'f', 10);

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("points"), points);

    _queryMode = QueryModeCarpet;
    _carpetStatsOnly = statsOnly;

    _sendQuery(QStringLiteral("/carpet"), query);
}

void TerrainAirMapQuery::_sendQuery(const QString& path, const QUrlQuery& urlQuery)
{
    QUrl url(QStringLiteral("https://api.airmap.com/elevation/v1/ele") + path);
    qCDebug(TerrainAirmapQueryLog) << "_sendQuery" << url;
    url.setQuery(urlQuery);

    QNetworkRequest request(url);

    QSslConfiguration sslConf = request.sslConfiguration();
    sslConf.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(sslConf);

    QNetworkProxy tProxy;
    tProxy.setType(QNetworkProxy::DefaultProxy);
    _networkManager.setProxy(tProxy);

    QNetworkReply* networkReply = _networkManager.get(request);
    if (!networkReply) {
        qCWarning(TerrainAirmapQueryLog) << "QNetworkManager::Get did not return QNetworkReply";
        _requestFailed();
        return;
    }
    QGCFileDownload::setIgnoreSSLErrorsIfNeeded(*networkReply);

    connect(networkReply, &QNetworkReply::finished, this, &TerrainAirMapQuery::_requestFinished);
    connect(networkReply, &QNetworkReply::sslErrors, this, &TerrainAirMapQuery::_sslErrors);

    connect(networkReply, &QNetworkReply::errorOccurred, this, &TerrainAirMapQuery::_requestError);
}

void TerrainAirMapQuery::_requestError(QNetworkReply::NetworkError code)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(QObject::sender());

    if (code != QNetworkReply::NoError) {
        qCWarning(TerrainAirmapQueryLog) << "_requestError error:url:data" << reply->error() << reply->url() << reply->readAll();
        return;
    }
}

void TerrainAirMapQuery::_sslErrors(const QList<QSslError> &errors)
{
    for (const auto &error : errors) {
        qCWarning(TerrainAirmapQueryLog) << "SSL error: " << error.errorString();

        const auto &certificate = error.certificate();
        if (!certificate.isNull()) {
            qCWarning(TerrainAirmapQueryLog) << "SSL Certificate problem: " << certificate.toText();
        }
    }
}

void TerrainAirMapQuery::_requestFinished(void)
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(QObject::sender());

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(TerrainAirmapQueryLog) << "_requestFinished error:url:data" << reply->error() << reply->url() << reply->readAll();
        reply->deleteLater();
        _requestFailed();
        return;
    }

    QByteArray responseBytes = reply->readAll();
    reply->deleteLater();

    // Convert the response to Json
    QJsonParseError parseError;
    QJsonDocument responseJson = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(TerrainAirmapQueryLog) << "_requestFinished unable to parse json:" << parseError.errorString();
        _requestFailed();
        return;
    }

    // Check airmap reponse status
    QJsonObject rootObject = responseJson.object();
    QString status = rootObject["status"].toString();
    if (status != "success") {
        qCWarning(TerrainAirmapQueryLog) << "_requestFinished status != success:" << status;
        _requestFailed();
        return;
    }

    // Send back data
    const QJsonValue& jsonData = rootObject["data"];
    qCDebug(TerrainAirmapQueryLog) << "_requestFinished success";
    switch (_queryMode) {
    case QueryModeCoordinates:
        emit _parseCoordinateData(jsonData);
        break;
    case QueryModePath:
        emit _parsePathData(jsonData);
        break;
    case QueryModeCarpet:
        emit _parseCarpetData(jsonData);
        break;
    }
}

void TerrainAirMapQuery::_requestFailed(void)
{
    switch (_queryMode) {
    case QueryModeCoordinates:
        emit coordinateHeightsReceived(false /* success */, QList<double>() /* heights */);
        break;
    case QueryModePath:
        emit pathHeightsReceived(false /* success */, qQNaN() /* latStep */, qQNaN() /* lonStep */, QList<double>() /* heights */);
        break;
    case QueryModeCarpet:
        emit carpetHeightsReceived(false /* success */, qQNaN() /* minHeight */, qQNaN() /* maxHeight */, QList<QList<double>>() /* carpet */);
        break;
    }
}

void TerrainAirMapQuery::_parseCoordinateData(const QJsonValue& coordinateJson)
{
    QList<double> heights;
    const QJsonArray& dataArray = coordinateJson.toArray();
    for (int i = 0; i < dataArray.count(); i++) {
        heights.append(dataArray[i].toDouble());
    }

    emit coordinateHeightsReceived(true /* success */, heights);
}

void TerrainAirMapQuery::_parsePathData(const QJsonValue& pathJson)
{
    QJsonObject jsonObject =    pathJson.toArray()[0].toObject();
    QJsonArray stepArray =      jsonObject["step"].toArray();
    QJsonArray profileArray =   jsonObject["profile"].toArray();

    double latStep = stepArray[0].toDouble();
    double lonStep = stepArray[1].toDouble();

    QList<double> heights;
    for (QJsonValue profileValue: profileArray) {
        heights.append(profileValue.toDouble());
    }

    emit pathHeightsReceived(true /* success */, latStep, lonStep, heights);
}

void TerrainAirMapQuery::_parseCarpetData(const QJsonValue& carpetJson)
{
    QJsonObject jsonObject =    carpetJson.toArray()[0].toObject();

    QJsonObject statsObject =   jsonObject["stats"].toObject();
    double      minHeight =     statsObject["min"].toDouble();
    double      maxHeight =     statsObject["max"].toDouble();

    QList<QList<double>> carpet;
    if (!_carpetStatsOnly) {
        QJsonArray carpetArray =   jsonObject["carpet"].toArray();

        for (int i=0; i<carpetArray.count(); i++) {
            QJsonArray rowArray = carpetArray[i].toArray();
            carpet.append(QList<double>());

            for (int j=0; j<rowArray.count(); j++) {
                double height = rowArray[j].toDouble();
                carpet.last().append(height);
            }
        }
    }

    emit carpetHeightsReceived(true /*success*/, minHeight, maxHeight, carpet);
}

TerrainOfflineAirMapQuery::TerrainOfflineAirMapQuery(QObject* parent)
    : TerrainQueryInterface(parent)
{
    qCDebug(TerrainAirmapQueryVerboseLog) << "supportsSsl" << QSslSocket::supportsSsl() << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();
}

void TerrainOfflineAirMapQuery::requestCoordinateHeights(const QList<QGeoCoordinate>& coordinates)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCoordinateHeights(coordinates);
        return;
    }

    if (coordinates.length() == 0) {
        return;
    }

    TerrainTileManager::instance()->addCoordinateQuery(this, coordinates);
}

void TerrainOfflineAirMapQuery::requestPathHeights(const QGeoCoordinate& fromCoord, const QGeoCoordinate& toCoord)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestPathHeights(fromCoord, toCoord);
        return;
    }

    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

void TerrainOfflineAirMapQuery::requestCarpetHeights(const QGeoCoordinate& swCoord, const QGeoCoordinate& neCoord, bool statsOnly)
{
    if (qgcApp()->runningUnitTests()) {
        UnitTestTerrainQuery(this).requestCarpetHeights(swCoord, neCoord, statsOnly);
        return;
    }

    // TODO
    Q_UNUSED(swCoord);
    Q_UNUSED(neCoord);
    Q_UNUSED(statsOnly);
    qWarning() << "Carpet queries are currently not supported from offline air map data";
}
