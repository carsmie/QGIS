#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include "qgsapplication.h"
#include "qgsproject.h"
#include "qgsprogressiveprojectloader.h"
#include "qgsmessagelog.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    // Initialize QGIS Application
    QgsApplication::init();
    QgsApplication::createDatabase();
    
    std::cout << "Testing QGIS Progressive Project Loader..." << std::endl;
    std::cout << "Loading test.qgs file..." << std::endl;
    
    // Create progressive project loader with optimized configuration
    QgsProgressiveProjectLoader loader;
    QgsProgressiveProjectLoader::LoadingConfig config;
    config.suppressNetworkWarnings = true;
    config.skipInaccessibleLayers = true;
    config.enableOfflineMode = false; // Keep false to test remote layer handling
    config.networkTimeoutMs = 3000;   // Short timeout
    config.maxNetworkRetries = 1;     // Minimal retries
    
    // Load the project
    QgsProject project;
    QString projectPath = "/home/miecar/repos/QGIS/testfile/test.qgs";
    
    bool success = loader.loadProjectWithConfig(projectPath, config, &project);
    
    if (success) {
        std::cout << "Project loaded successfully!" << std::endl;
        std::cout << "Layers in project: " << project.mapLayers().size() << std::endl;
    } else {
        std::cout << "Failed to load project." << std::endl;
    }
    
    // Cleanup
    QgsApplication::exitQgis();
    
    return success ? 0 : 1;
}