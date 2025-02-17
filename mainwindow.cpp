#include "mainwindow.h"
#include "ui_mainwindow.h"

static vector<QHotkey *> hotkeys;

static string configFolder;
static string soundFilesConfig;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    ui->tabWidget->setTabsClosable(true);
    ui->tabWidget->setMovable(true);
    ui->stopButton->setDisabled(true);

    ui->remoteVolumeSlider->setStyle(new ClickableSliderStyle(ui->remoteVolumeSlider->style()));
    ui->localVolumeSlider->setStyle(new ClickableSliderStyle(ui->localVolumeSlider->style()));

    // Set the config variables
    configFolder = QStandardPaths::standardLocations(QStandardPaths::ConfigLocation)[0].toStdString() + "/" + windowTitle().toStdString();

    // Create config folder
    QString configFolderQ = QString::fromStdString(configFolder);
    QDir dir;
    QFile configFolderFile(configFolderQ);
    if (!configFolderFile.exists()) {
        dir.mkdir(configFolderQ);
    }
    
    soundFilesConfig = configFolder + "/sounds.json";

    soundPlayback = new SoundPlayback(this, ui);

    settingsDialog = new SettingsDialog(this, configFolder, soundPlayback);

    // We unload the modules first to remove any possible leftovers
    //TODO: Only remove modules created by Soundboard
    system("pacmd unload-module module-null-sink");
    system("pacmd unload-module module-loopback");

    // Create null sink
    system("pacmd load-module module-null-sink sink_name=soundboard_sink sink_properties=device.description=Soundboard-Sink");

    // get default input device
    string defaultInput = "";
    char cmd[] = "pacmd dump";
    string result = soundPlayback->getCommandOutput(cmd);
    regex reg(R"rgx(set-default-source (.+))rgx");
    smatch sm;
    regex_search(result, sm, reg);
    defaultInput = sm[1].str();

    // Create loopback for input
    if (defaultInput != "")
    {
        cout << "Found default input device " << defaultInput << endl;
        auto createLoopBack = "pacmd load-module module-loopback source=\"" + defaultInput + "\" sink=\"soundboard_sink\"";
        system(createLoopBack.c_str());
    }

    loadSoundFiles();
    soundPlayback->loadSources();

    // we need to update the buttons if the program starts because the first tab may be a directory tab
    this->on_tabWidget_currentChanged(0);

    // add CTRL + Q shortcut: quit
    auto shortcut = new QShortcut(this);
    shortcut->setKey(Qt::CTRL + Qt::Key_Q);
    connect(shortcut, SIGNAL(activated()), this, SLOT(slotShortcutCtrlQ()));

    // add CTRL + F shortcut: search
    auto searchShortcut = new QShortcut(this);
    searchShortcut->setKey(Qt::CTRL + Qt::Key_F);
    connect(searchShortcut, SIGNAL(activated()), SLOT(slotShortcutCtrlF()));

    searchView = new SearchView(this, ui->tabWidget, soundPlayback);
    searchView->hide();
    searchView->setAllowedAreas(Qt::RightDockWidgetArea);

    this->addDockWidget(Qt::RightDockWidgetArea, searchView);

    connect(ui->searchButton, &QPushButton::clicked, [&]() {
        if (searchView->isHidden()) {
            searchView->show();
        } else {
            searchView->hide();
        }
    });

}

void MainWindow::slotShortcutCtrlQ()
{
    close();
}

void MainWindow::slotShortcutCtrlF()
{
    if (searchView->isHidden()) {
        searchView->show();
    } else {
        searchView->hide();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    //TODO: Only remove modules created by Soundboard
    system("pacmd unload-module module-null-sink");
    system("pacmd unload-module module-loopback");
    //TODO: Switch all recording streams back to default device
    event->accept();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::syncVolume(bool remote)
{
    // Get volume from slider
    int localValue = ui->localVolumeSlider->value();
    int remoteValue = ui->remoteVolumeSlider->value();

    if (ui->syncCheckBox->isChecked()) {
        if (remote) {
            ui->localVolumeSlider->setValue(remoteValue);
        } else {
            ui->remoteVolumeSlider->setValue(localValue);
        }
    }

    // TODO: this is disabled until I find a good solution
    /*

    char cmd[] = "pacmd list-sink-inputs";
    string result = getCommandOutput(cmd);
    string delimiter = "\n";
    size_t pos = 0;
    string currentLine;

    // Tell me if there is a better way to parse the pulseaudio output
    regex reg(R"rgx(((index: (\d+)))|(driver: )(.*)|(state: )(.*)|(flags: )(.*)|(source: .*)(<(.*)>)|(muted: )(.{0,3})|([a-zA-Z-.0-9_]*)\ =\ (\"(.*)\"))rgx");
    smatch sm;

    PulseAudioPlaybackStream *current = nullptr;

    while ((pos = result.find(delimiter)) != string::npos)
    {
        currentLine = result.substr(0, pos);
        if (regex_search(currentLine, sm, reg))
        {
            auto index = sm[3];
            if (index.length() > 0)
            {
                if (current)
                {
                    soundPlayback->checkAndChangeVolume(current, localValue);
                }

                current = new PulseAudioPlaybackStream();
                current->index = stoi(index);
            }
            else
            {
                auto propertyName = sm[15];
                auto propertyValue = sm[17];
                if (propertyName.length() > 0)
                {
                    if (propertyName == "application.name")
                    {
                        current->applicationName = propertyValue.str();
                    }
                }
            }
        }

        result.erase(0, pos + delimiter.length());
    }
    soundPlayback->checkAndChangeVolume(current, localValue);
    */
}

// Sync volume when the slider value has changed
void MainWindow::on_localVolumeSlider_valueChanged(int value)
{
    syncVolume(false);
}
void MainWindow::on_remoteVolumeSlider_valueChanged(int value)
{
    syncVolume(true);
}


void MainWindow::on_refreshAppsButton_clicked()
{
    soundPlayback->loadSources();
}

void MainWindow::on_stopButton_clicked()
{
    soundPlayback->stopSound();
}

void MainWindow::on_addFolderButton_clicked()
{
    auto selectedFolder = QFileDialog::getExistingDirectory(this, ("Select folder"), QDir::homePath());

    if (selectedFolder != "")
    {
        QDir directory(selectedFolder);
        QFileInfo fileInfo(selectedFolder);
        auto created = createTab(fileInfo.fileName());

        created->directory = directory.absolutePath().toStdString();

        QStringList files = directory.entryList({"*.mp3", "*.wav", "*.ogg"}, QDir::Files);
        for (auto fileName : files)
        {
            QFile file(directory.absoluteFilePath(fileName));
            addSoundToView(file, created);
        }
        saveSoundFiles();
    }
}

void MainWindow::on_refreshFolderButton_clicked()
{
    auto view = getActiveView();
    if (view) {
        this->refreshFolder(view);
    }
}

void MainWindow::refreshFolder(QSoundsList *view) {
    QStringList items;
    for (auto viewItem : view->findItems("*", Qt::MatchWildcard))
    {
        items.push_back(viewItem->toolTip());
    }

    const QDir directory(QString::fromStdString(view->directory));
    const QStringList files = directory.entryList({"*.mp3", "*.wav", "*.ogg"}, QDir::Files);
    QStringList filesAbsolute;

    for (auto fileName : files)
    {
        const auto absolutePath = directory.absoluteFilePath(fileName);
        filesAbsolute.push_back(absolutePath);


        // add new ones
        if (!LContains(items, absolutePath)) {
            cout << "adding " << absolutePath.toStdString() << endl;
            QFile file(absolutePath);
            addSoundToView(file, view);
        }
    }

    auto list = view->findItems("*", Qt::MatchWildcard);

    // remove old ones
    for (const auto item : list) {
        if (!LContains(filesAbsolute, item->toolTip())) {
           cout << "removing " << item->toolTip().toStdString() << endl;
           this->removeSound((SoundListWidgetItem*) item);
        }
    }

    saveSoundFiles();
}

void MainWindow::addSoundToView(QFile &file, QListWidget *widget)
{
    QFileInfo fileInfo(file);

    auto path = fileInfo.absoluteFilePath().toStdString();

    for (QListWidgetItem *item : widget->findItems("*", Qt::MatchWildcard))
    {
        // Check if Sound is already added
        if (path == item->toolTip().toStdString())
        {
            auto already = "The sound " + item->text().toStdString() + " is already in the list";
            QMessageBox::warning(this, "", tr(already.c_str()), QMessageBox::Ok);
            return;
        }
    }

    auto item = new SoundListWidgetItem();
    item->setText(fileInfo.completeBaseName());
    item->setToolTip(fileInfo.absoluteFilePath());
    widget->addItem(item);
}

void MainWindow::on_addSoundButton_clicked()
{
    if (!getActiveView())
    {
        createTab("Main");
    }
    QStringList selectedFiles = QFileDialog::getOpenFileNames(this, tr("Select file"), QDir::homePath(), tr("Sound files (*.mp3 *.wav *.ogg)"));
    for (auto selectedFile : selectedFiles)
    {
        if (selectedFile != "")
        {
            QFile file(selectedFile);
            addSoundToView(file, getActiveView());
        }
    }
    saveSoundFiles();
}

void MainWindow::on_settingsButton_clicked()
{
    settingsDialog->exec();
}

void MainWindow::on_soundsListWidget_itemDoubleClicked(QListWidgetItem *listWidgetItem)
{
    if (listWidgetItem)
    {
        soundPlayback->playSound(listWidgetItem->toolTip().toStdString());
    }
}

void MainWindow::on_removeSoundButton_clicked()
{
    if (getActiveView())
    {
        SoundListWidgetItem *it = getSelectedItem();
        this->removeSound(it);
    }
}

void MainWindow::removeSound(SoundListWidgetItem *it) {
    if (it)
    {
        unregisterHotkey(it);
        delete it;
        saveSoundFiles();
    }
}

void MainWindow::on_clearSoundsButton_clicked()
{
    if (getActiveView())
    {
        QMessageBox::StandardButton resBtn = QMessageBox::question(this, "Clear sounds", tr("Are you sure?\n"), QMessageBox::No | QMessageBox::Yes, QMessageBox::Yes);
        if (resBtn == QMessageBox::Yes)
        {
            clearSoundFiles();
            saveSoundFiles();
        }
    }
}

SoundListWidgetItem *MainWindow::getSelectedItem()
{
    if (getActiveView())
    {
        return (SoundListWidgetItem*) getActiveView()->item(getActiveView()->currentRow());
    }
    return nullptr;
}

void MainWindow::on_playSoundButton_clicked()
{
    SoundListWidgetItem *it = getSelectedItem();
    if (it)
    {
        soundPlayback->playSound(it->toolTip().toStdString());
    }
}

void MainWindow::on_addTabButton_clicked()
{
    bool ok;
    QString text = QInputDialog::getText(this, "Add a tab", "Tab Text:", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty())
    {
        createTab(text);
        saveSoundFiles();
    }
}

void MainWindow::on_setHotkeyButton_clicked()
{
    SoundListWidgetItem *it = getSelectedItem();
    if (it)
    {
        SetHotkeyDialog shd(this, it);
        auto clicked = shd.exec();
        if (clicked == 1)
        {
            auto given = shd.getSequence();
            if (!given.isNull()) {
                registerHotkey(it, given.toString());
            } else {
                unregisterHotkey(it);
            }

            saveSoundFiles();
        }
    }
}

void MainWindow::registerHotkey(SoundListWidgetItem* it, QString keys)
{
    // Unregister previous hotkey
    unregisterHotkey(it);

    it->setHotkey(keys);
    cout << "register " << keys.toStdString() << endl;
    auto neger = QKeySequence(keys);

    auto hotkey = new QHotkey(QKeySequence(keys), true, this);

    if (hotkey->isRegistered())
    {
        hotkeys.push_back(hotkey);
        auto toPlay = it->toolTip().toStdString();
        connect(hotkey, &QHotkey::activated, this, [=]() {
            soundPlayback->playSound(toPlay);
        });
    }
    else
    {
        unregisterHotkey(it);
        QMessageBox::warning(this, "Could not register " + keys, "Either the key combination is not valid or it's not possible to use this combination (Maybe another program is using it)", QMessageBox::Ok);
    }
}

bool compareChar(char &c1, char &c2)
{
    if (c1 == c2)
        return true;
    else if (toupper(c1) == toupper(c2))
        return true;
    return false;
}

bool caseInSensStringCompare(string &str1, string &str2)
{
    return ((str1.size() == str2.size()) &&
            equal(str1.begin(), str1.end(), str2.begin(), &compareChar));
}

void MainWindow::unregisterHotkey(SoundListWidgetItem *it)
{
    auto previousHotkey = it->hotkey;
    if (!previousHotkey.isNull())
    {
        auto previousHotkeyStr = previousHotkey.toString().toStdString();
        cout << "unregister " << previousHotkeyStr << endl;

        for (QHotkey *hotkey : hotkeys)
        {
            auto hotkeyStr = hotkey->shortcut().toString().toStdString();
            if (caseInSensStringCompare(hotkeyStr, previousHotkeyStr))
            {
                delete hotkey;
            }
        }

        // Reset Data
        it->setHotkey(QVariant());
    }
}

void MainWindow::on_tabWidget_tabBarDoubleClicked(int index)
{
    bool ok;
    QString text = QInputDialog::getText(this, "Rename tab", "Tab Text:", QLineEdit::Normal, ui->tabWidget->tabText(index), &ok);
    if (ok && !text.isEmpty())
    {
        ui->tabWidget->setTabText(index, text);
        saveSoundFiles();
    }
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    QMessageBox::StandardButton resBtn = QMessageBox::question(this, "Delete tab", tr("Are you sure?\n"), QMessageBox::No | QMessageBox::Yes, QMessageBox::Yes);
    if (resBtn == QMessageBox::Yes)
    {
        ui->tabWidget->removeTab(index);
        saveSoundFiles();
    }
}

QSoundsList *MainWindow::createTab(QString title)
{
    auto soundsListWidget = new QSoundsList();
    // why do we set the object name?
    soundsListWidget->setObjectName(title);
    connect(soundsListWidget, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(on_soundsListWidget_itemDoubleClicked(QListWidgetItem *)));
    ui->tabWidget->addTab(soundsListWidget, title);
    return soundsListWidget;
}

void MainWindow::clearSoundFiles()
{
    if (getActiveView())
    {
        while (getActiveView()->count() > 0)
        {
            getActiveView()->takeItem(0);
        }
    }
}

QSoundsList *MainWindow::getActiveView()
{
    return (QSoundsList *)ui->tabWidget->widget(ui->tabWidget->currentIndex());
}

void MainWindow::saveSoundFiles()
{
    json jsonTabs = json::array();

    for (auto i = 0; i < ui->tabWidget->count(); i++)
    {
        auto title = ui->tabWidget->tabText(i).toStdString();
        QSoundsList *listWidget = (QSoundsList *)ui->tabWidget->widget(i);

        json tabJson;
        json tabJsonSounds = json::array();

        // if it is a directory we just save the path and update the sounds from there later
        if (listWidget->directory.length() > 0) {
            tabJson["directory"] = listWidget->directory;
        }

        for (auto *_item : listWidget->findItems("*", Qt::MatchWildcard))
        {
            auto item = (SoundListWidgetItem*) _item;
            json j;
            j["name"] = item->text().toStdString();
            // TODO: make the path relative when it's a folder tab
            j["path"] = item->toolTip().toStdString();

            auto hotkey = item->hotkey;
            if (!hotkey.isNull())
            {
                auto hotkeyStr = hotkey.toString().toStdString();
                j["hotkey"] = hotkeyStr;
            }

            tabJsonSounds.push_back(j);
        }

        tabJson["title"] = title;
        tabJson["sounds"] = tabJsonSounds;

        jsonTabs.push_back(tabJson);
    }

    ofstream myfile;
    myfile.open(soundFilesConfig);
    myfile << jsonTabs.dump();
    myfile.close();
}

void MainWindow::loadSoundFiles()
{
    ifstream fileIn(soundFilesConfig);
    if (fileIn.is_open())
    {
        clearSoundFiles();

        json j = json::parse(fileIn);


        for (auto& tabItem : j.items())
        {
            const auto item = tabItem.value();

            const auto titleItem = item.find("title");
            const auto directoryItem = item.find("directory");
            const auto soundsItem = item.find("sounds");

            if (titleItem == item.end() || soundsItem == item.end()) {
                cout << item.dump() << " is not a valid tab" << endl;
                continue;
            }

            const auto title = titleItem->get<string>();
            const auto sounds = soundsItem->get<vector<json>>();

            const auto soundsListWidget = createTab(title.c_str());

            for (auto _child : sounds)
            {
                auto soundName = _child["name"];
                auto soundPath = _child["path"];
                remove(soundPath.begin(), soundPath.end(), '"');

                auto item = new SoundListWidgetItem();
                item->setText(QString::fromStdString(soundName));
                item->setToolTip(QString::fromStdString(soundPath));

                auto soundHotkey = _child["hotkey"];
                if (!soundHotkey.is_null())
                {
                    // Set hotkey back
                    registerHotkey(item, QString::fromStdString(soundHotkey));
                }
                soundsListWidget->addItem(item);
            }

            if (directoryItem != item.end()) {
                const auto directory = directoryItem->get<string>();
                // it is a directory category so we set the property
                soundsListWidget->directory = directory;
                this->refreshFolder(soundsListWidget);
            }

        }

        fileIn.close();
    }
}

// we need this to update the buttons if the tab switched
void MainWindow::on_tabWidget_currentChanged(int index)
{
    QSoundsList* switchedTo = (QSoundsList *)ui->tabWidget->widget(index);
    if (switchedTo) {
        bool isFolderTab = switchedTo->directory.length() > 0;

        this->ui->addSoundButton->setVisible(!isFolderTab);
        this->ui->removeSoundButton->setVisible(!isFolderTab);
        this->ui->clearSoundsButton->setVisible(!isFolderTab);
        this->ui->refreshFolderButton->setVisible(isFolderTab);
        this->ui->setHotkeyButton->setVisible(true);

    } else {
        this->ui->addSoundButton->setVisible(true);
        this->ui->removeSoundButton->setVisible(false);
        this->ui->clearSoundsButton->setVisible(false);
        this->ui->refreshFolderButton->setVisible(false);
        this->ui->setHotkeyButton->setVisible(false);
    }
}
