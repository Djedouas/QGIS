/***************************************************************************
                        qgsalgorithmdeletevertex.cpp
                        ---------------------
   begin                : June 2024
   copyright            : (C) 2024 by Jacky Volpes
   email                : jacky dot volpes at oslandia dot com
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsalgorithmmergepolygons.h"
#include "qgsgeometryareacheck.h"
#include "qgsvectordataproviderfeaturepool.h"
#include "qgsgeometrycheckerror.h"
#include "qgsvectorfilewriter.h"

///@cond PRIVATE

auto QgsMergePolygonsAlgorithm::name() const -> QString
{
  return QStringLiteral( "mergepolygons" );
}

auto QgsMergePolygonsAlgorithm::displayName() const -> QString
{
  return QObject::tr( "Merge neighboring polygons" );
}

auto QgsMergePolygonsAlgorithm::tags() const -> QStringList
{
  return QObject::tr( "merge,polygons,neighbor" ).split( ',' );
}

auto QgsMergePolygonsAlgorithm::group() const -> QString
{
  return QObject::tr( "Fix geometry" );
}

auto QgsMergePolygonsAlgorithm::groupId() const -> QString
{
  return QStringLiteral( "fixgeometry" );
}

auto QgsMergePolygonsAlgorithm::shortHelpString() const -> QString
{
  return QObject::tr( "This algorithm merges neighboring polygons according to the chosen method." );
}

auto QgsMergePolygonsAlgorithm::createInstance() const -> QgsMergePolygonsAlgorithm *
{
  return new QgsMergePolygonsAlgorithm();
}

void QgsMergePolygonsAlgorithm::initAlgorithm( const QVariantMap &configuration )
{
  Q_UNUSED( configuration )

  // Inputs
  addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "INPUT" ), QObject::tr( "Input layer" ),
                QList< int >() << static_cast<int>( Qgis::ProcessingSourceType::VectorPolygon ) )
              );
  addParameter( new QgsProcessingParameterFeatureSource( QStringLiteral( "ERRORS" ), QObject::tr( "Errors layer" ),
                QList< int >() << static_cast<int>( Qgis::ProcessingSourceType::VectorPoint ) )
              );

  QVariantMap config;
  QStringList methods;
  {
    QgsGeometryCheckContext *context;  //!\ uninitialized context to retreive resolution methods ontly (which should perhaps be a static method?)
    QList<QgsGeometryCheckResolutionMethod> checkMethods = QgsGeometryAreaCheck( context, config ).availableResolutionMethods();
    std::transform( checkMethods.cbegin(), checkMethods.cend() - 2, std::inserter( methods, methods.begin() ),
    []( const QgsGeometryCheckResolutionMethod & checkMethod ) { return checkMethod.name(); } );
  }
  addParameter( new QgsProcessingParameterEnum( QStringLiteral( "METHOD" ), QObject::tr( "Method" ), methods ) );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "MERGE_ATTRIBUTE" ), QObject::tr( "Field to consider when merging polygons with the identical attribue method" ),
                  QStringLiteral( "" ), QStringLiteral( "INPUT" ),
                  Qgis::ProcessingFieldParameterDataType::Any, false, true )
              );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "FEAT_ID" ), QObject::tr( "Field of feature ID" ),
                  QStringLiteral( "gc_featid" ), QStringLiteral( "ERRORS" ),
                  Qgis::ProcessingFieldParameterDataType::Numeric, false, true )
              );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "PART_IDX" ), QObject::tr( "Field of part index" ),
                  QStringLiteral( "gc_partidx" ), QStringLiteral( "ERRORS" ),
                  Qgis::ProcessingFieldParameterDataType::Numeric, false, true )
              );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "RING_IDX" ), QObject::tr( "Field of ring index" ),
                  QStringLiteral( "gc_ringidx" ), QStringLiteral( "ERRORS" ),
                  Qgis::ProcessingFieldParameterDataType::Numeric, false, true )
              );
  addParameter( new QgsProcessingParameterField(
                  QStringLiteral( "VERTEX_IDX" ), QObject::tr( "Field of vertex index" ),
                  QStringLiteral( "gc_vertidx" ), QStringLiteral( "ERRORS" ),
                  Qgis::ProcessingFieldParameterDataType::Numeric, false, true )
              );

  // Outputs
  addParameter( new QgsProcessingParameterVectorDestination( QStringLiteral( "OUTPUT" ), QObject::tr( "Output layer" ), Qgis::ProcessingSourceType::VectorPolygon ) );
  addParameter( new QgsProcessingParameterFeatureSink( QStringLiteral( "REPORT" ), QObject::tr( "Report layer" ), Qgis::ProcessingSourceType::VectorPoint ) );

  std::unique_ptr< QgsProcessingParameterNumber > tolerance = std::make_unique< QgsProcessingParameterNumber >( QStringLiteral( "TOLERANCE" ),
      QObject::tr( "Tolerance" ), Qgis::ProcessingNumberParameterType::Integer, 8, false, 1, 13 );
  tolerance->setFlags( tolerance->flags() | Qgis::ProcessingParameterFlag::Advanced );
  addParameter( tolerance.release() );
}

auto QgsMergePolygonsAlgorithm::processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) -> QVariantMap
{
  QgsVectorLayer *inputLayer = parameterAsVectorLayer( parameters, QStringLiteral( "INPUT" ), context );

  std::unique_ptr< QgsProcessingFeatureSource > errors( parameterAsSource( parameters, QStringLiteral( "ERRORS" ), context ) );
  if ( !errors )
    throw QgsProcessingException( invalidSourceError( parameters, QStringLiteral( "ERRORS" ) ) );

  QgsProcessingMultiStepFeedback multiStepFeedback( 2, feedback );

  QString featIdFieldName = parameterAsString( parameters, QStringLiteral( "FEAT_ID" ), context );
  QString partIdxFieldName = parameterAsString( parameters, QStringLiteral( "PART_IDX" ), context );
  QString ringIdxFieldName = parameterAsString( parameters, QStringLiteral( "RING_IDX" ), context );
  QString vertexIdxFieldName = parameterAsString( parameters, QStringLiteral( "VERTEX_IDX" ), context );
  QString mergeAttributeName = parameterAsString( parameters, QStringLiteral( "MERGE_ATTRIBUTE" ), context );
  int method = parameterAsEnum( parameters, QStringLiteral( "METHOD" ), context );
  std::cout << "########### method " << method << std::endl;

  if ( !errors->fields().names().contains( featIdFieldName ) )
    throw QgsProcessingException( QObject::tr( "Field %1 does not exist in errors layer" ).arg( featIdFieldName ) );
  bool typeOk = false;
  switch ( errors->fields().field( featIdFieldName ).type() )
  {
    case QMetaType::Type::Int:
    case QMetaType::Type::UInt:
    case QMetaType::Type::LongLong:
    case QMetaType::Type::ULongLong:
      typeOk = true;
      break;
    default:
      break;
  }
  if ( !typeOk )
    throw QgsProcessingException( QObject::tr( "Field %1 does not have the correct type (integer needed)" ).arg( featIdFieldName ) );

  QString dest_output = parameterAsOutputLayer( parameters, QStringLiteral( "OUTPUT" ), context );

  QString dest_report;
  QgsFields reportFields = errors->fields();
  reportFields.append( QgsField( QStringLiteral( "report" ), QMetaType::QString ) );
  std::unique_ptr< QgsFeatureSink > sink_report( parameterAsSink( parameters, QStringLiteral( "REPORT" ), context, dest_report, reportFields, errors->wkbType(), errors->sourceCrs() ) );
  if ( !sink_report )
    throw QgsProcessingException( invalidSinkError( parameters, QStringLiteral( "REPORT" ) ) );

  QgsProject *project = inputLayer->project() ? inputLayer->project() : QgsProject::instance();
  std::unique_ptr<QgsGeometryCheckContext> checkContext = std::make_unique<QgsGeometryCheckContext>( mTolerance, inputLayer->sourceCrs(), project->transformContext(), project );
  QStringList messages;
  QVariantMap configurationCheck;
  configurationCheck.insert( "areaThreshold", std::numeric_limits<double>::max() ); // we know that every feature to process is an error
  const QgsGeometryAreaCheck check( checkContext.get(), configurationCheck );

  QgsVectorLayer *fixedLayer = inputLayer->materialize( QgsFeatureRequest() );
  std::unique_ptr<QgsFeaturePool> featurePool = std::make_unique<QgsVectorDataProviderFeaturePool>( fixedLayer, false );
  QMap<QString, QgsFeaturePool *> featurePools;
  featurePools.insert( fixedLayer->id(), featurePool.get() );

  QMap<QString, int> attributeIndex;
  if ( method == QgsGeometryAreaCheck::ResolutionMethod::MergeIdenticalAttribute )
  {
    if ( !fixedLayer->fields().names().contains( mergeAttributeName ) )
      throw QgsProcessingException( QObject::tr( "Field %1 does not exist in input layer" ).arg( mergeAttributeName ) );
    attributeIndex.insert( fixedLayer->id(), fixedLayer->fields().indexOf( mergeAttributeName ) );
  }

  QgsFeature errorFeature;
  QgsFeatureIterator errorFeaturesIt = errors->getFeatures();
  QgsFeatureList errorFeatures;
  while ( errorFeaturesIt.nextFeature( errorFeature ) )
    errorFeatures.append( errorFeature );
  std::sort( errorFeatures.begin(), errorFeatures.end(), [&featIdFieldName]( const QgsFeature & lhs, const QgsFeature & rhs )
  {
    return lhs.attribute( featIdFieldName ).toLongLong() > rhs.attribute( featIdFieldName ).toLongLong();
  } );
  for ( QgsFeature errorFeature : errorFeatures )
  {
    QgsFeature reportFeature;
    reportFeature.setFields( reportFields );
    reportFeature.setGeometry( errorFeature.geometry() );

    std::cout << "getting feature with id " <<  errorFeature.attribute( featIdFieldName ).toLongLong() << std::endl;
    const QgsFeature inputFeature = fixedLayer->getFeature( errorFeature.attribute( featIdFieldName ).toLongLong() );
    std::cout << "fixing feature " <<  inputFeature.attribute( "id" ).toLongLong() << std::endl;
    if ( !inputFeature.isValid() )
    {
      reportFeature.setAttributes( errorFeature.attributes() << QObject::tr( "Source feature not found" ) );
      continue;
    }
    if ( inputFeature.geometry().constGet() == nullptr )
    {
      reportFeature.setAttributes( errorFeature.attributes() << QObject::tr( "Feature geometry is null" ) );
      continue;
    }
    if ( QgsGeometryCheckerUtils::getGeomPart( inputFeature.geometry().constGet(), errorFeature.attribute( partIdxFieldName ).toInt() ) == nullptr )
    {
      reportFeature.setAttributes( errorFeature.attributes() << QObject::tr( "Feature geometry part is null" ) );
      continue;
    }

    QgsGeometryCheckError checkError = QgsGeometryCheckError(
                                         &check,
                                         QgsGeometryCheckerUtils::LayerFeature( featurePool.get(), inputFeature, checkContext.get(), false ),
                                         errorFeature.geometry().asPoint(),
                                         QgsVertexId(
                                           errorFeature.attribute( partIdxFieldName ).toInt(),
                                           errorFeature.attribute( ringIdxFieldName ).toInt(),
                                           errorFeature.attribute( vertexIdxFieldName ).toInt() )
                                       );
    QgsGeometryCheck::Changes changes;
    check.fixError( featurePools, &checkError, method, attributeIndex, changes );

    // switch ( checkError.status() )
    // {
    //   case QgsGeometryCheckError::StatusObsolete:
    //     std::cout << "Erreur obsolète !" << std::endl;
    //     break;
    //   case QgsGeometryCheckError::StatusFixed:
    //     std::cout << "Erreur corrigée" << std::endl;
    //     break;
    //   case QgsGeometryCheckError::StatusPending:
    //     std::cout << "Erreur pending" << std::endl;
    //     break;
    //   case QgsGeometryCheckError::StatusFixFailed:
    //     std::cout << "Erreur non corrigée" << std::endl;
    //     break;
    // }

    reportFeature.setAttributes( errorFeature.attributes() << checkError.resolutionMessage() );
    if ( !sink_report->addFeature( reportFeature, QgsFeatureSink::FastInsert ) )
      throw QgsProcessingException( writeFeatureError( sink_report.get(), parameters, QStringLiteral( "REPORT" ) ) );

    // std::cout << checkError.resolutionMessage().toStdString() << std::endl;

    // if ( changes.count() == 0 )
    //   std::cout << "AUCUN CHANGEMENT" << std::endl;

    // for ( auto layerId : changes.keys() )
    // {
    //   std::cout << "=========================" << std::endl;
    //   std::cout << "Layer ID: " << layerId.toStdString() << std::endl;
    //   auto layerChanges = changes[layerId];
    //   for ( auto featureId : layerChanges.keys() )
    //   {
    //     std::cout  << "-------------------" << std::endl;
    //     std::cout << "Feature ID: " << featureId << std::endl;
    //     for ( auto featureChange : layerChanges[featureId] )
    //     {
    //       switch ( featureChange.what )
    //       {
    //         case QgsGeometryCheck::ChangeWhat::ChangeNode:
    //           std::cout << "Noeud changé: ";
    //           break;
    //         case QgsGeometryCheck::ChangeWhat::ChangePart:
    //           std::cout << "Partie changée: ";
    //           break;
    //         case QgsGeometryCheck::ChangeWhat::ChangeRing:
    //           std::cout << "Anneau changé: ";
    //           break;
    //         case QgsGeometryCheck::ChangeWhat::ChangeFeature:
    //           std::cout << "Feature changée: ";
    //           break;
    //         default:
    //           std::cout << "Euh ??";
    //           break;
    //       }
    //       switch ( featureChange.type )
    //       {
    //         case QgsGeometryCheck::ChangeType::ChangeChanged:
    //           std::cout << "Mis à jour";
    //           break;
    //         case QgsGeometryCheck::ChangeType::ChangeAdded:
    //           std::cout << "Ajouté";
    //           break;
    //         case QgsGeometryCheck::ChangeType::ChangeRemoved:
    //           std::cout << "Supprimé";
    //           break;
    //         default:
    //           std::cout << "Euh ??";
    //           break;
    //       }
    //       std::cout << std::endl;
    //       auto vid = featureChange.vidx;
    //       std::cout << "part : "  << vid.part << ", ring : " << vid.ring << ", vertex : " << vid.vertex << std::endl;
    //     }
    //   }
    //   std::cout << "=========================" << std::endl;
    // }
  }

  QgsVectorFileWriter::writeAsVectorFormatV3( fixedLayer, dest_output, QgsCoordinateTransformContext(), QgsVectorFileWriter::SaveVectorOptions() );
  QVariantMap outputs;
  outputs.insert( QStringLiteral( "OUTPUT" ), dest_output );
  outputs.insert( QStringLiteral( "REPORT" ), dest_report );

  return outputs;
}

auto QgsMergePolygonsAlgorithm::prepareAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback * ) -> bool
{
  mTolerance = parameterAsInt( parameters, QStringLiteral( "TOLERANCE" ), context );

  return true;
}

auto QgsMergePolygonsAlgorithm::flags() const -> Qgis::ProcessingAlgorithmFlags
{
  return QgsProcessingAlgorithm::flags() | Qgis::ProcessingAlgorithmFlag::NoThreading;
}

///@endcond
