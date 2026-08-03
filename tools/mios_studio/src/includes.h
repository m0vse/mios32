#ifndef _INCLUDES_H
#define _INCLUDES_H

#include "JuceHeader.h"
#pragma warning( disable : 4018 )
#pragma warning( disable : 4244 )
#pragma warning( disable : 4100 )
#pragma warning( disable : 4189)

class MiosStudioProperties
{
public:
    MiosStudioProperties()
    {
        miosStudioStorageOptions.applicationName = "MIOS Studio";
        miosStudioStorageOptions.filenameSuffix  = ".xml";
        miosStudioStorageOptions.folderName      = "MIOS Studio";
        miosStudioStorageOptions.osxLibrarySubFolder = "Application Support";
        miosStudioStorageOptions.storageFormat   = PropertiesFile::storeAsXML;
        miosStudioStorageOptions.millisecondsBeforeSaving = 1000;
        applicationProperties.setStorageParameters(miosStudioStorageOptions);
    }
    
    ~MiosStudioProperties()
    {
        clearSingletonInstance();
    }
    
    ApplicationProperties &getProperties()
    {
        return (applicationProperties);
    }
    
    PropertiesFile *getCommonSettings(const bool returnIfReadOnly)
    {
        return (applicationProperties.getCommonSettings(returnIfReadOnly));
    }
    
    juce_DeclareSingleton (MiosStudioProperties, false)
    
private:
    ApplicationProperties applicationProperties;
    PropertiesFile::Options miosStudioStorageOptions;
};

#define T(x) String(x)
#define juce_malloc(x) malloc(x)
#define juce_free(x)   free(x)

inline void miosShowMessageBox(MessageBoxIconType iconType,
                               const String& title,
                               const String& message,
                               const String& buttonText,
                               Component* associatedComponent = nullptr)
{
#if JUCE_IOS
    AlertWindow::showMessageBoxAsync(iconType, title, message, buttonText, associatedComponent);
#else
    AlertWindow::showMessageBox(iconType, title, message, buttonText, associatedComponent);
#endif
}
#endif
