#!/usr/bin/env python3
"""The text 1.9.0 added, in the seven translated languages.

    python3 tools/lang_190.py          # update assets/lang/*.tsv (idempotent)
    python3 tools/lang_190.py --check  # only report: missing keys, characters the fonts lack

Each entry: English (exactly as in the source), then es, pt, fr, de, it, ja, ru.
DEAD lists keys the source no longer uses; they are removed."""
import sys
from pathlib import Path

LANGS = ["es", "pt", "fr", "de", "it", "ja", "ru"]
DEAD = ["Start at boot", "Welcome back", "Open VitaOS at power-on (needs the PS plugin)",
        "VitaOS opens at power-on (needs the PS plugin)", "That file would not play", "This song would not play",
        "Opens VitaOS at power-on and after games; PS goes to its Home. Hold L at power-on to skip plugins once."]
T = [
 ("Start at power-on", "Abrir al encender", "Abrir ao ligar", "Ouvrir au démarrage", "Beim Einschalten öffnen", "Apri all'accensione", "電源オンで開く", "Открывать при включении"),
 ("Set up", "Configurar", "Configurar", "Configurer", "Einrichten", "Configura", "設定する", "Настроить"),
 ("Needs taiHEN", "Requiere taiHEN", "Requer taiHEN", "taiHEN requis", "Braucht taiHEN", "Richiede taiHEN", "taiHENが必要", "Нужен taiHEN"),
 ("Set up by hand", "Configurar a mano", "Configurar à mão", "À configurer à la main", "Von Hand einrichten", "Configura a mano", "手動で設定", "Настроить вручную"),
 ("VitaOS opens when the Vita starts, and comes back after games",
  "VitaOS se abre al encender la Vita y vuelve después de los juegos",
  "O VitaOS abre ao ligar o Vita e volta depois dos jogos",
  "VitaOS s'ouvre au démarrage de la Vita et revient après les jeux",
  "VitaOS öffnet sich beim Einschalten und kehrt nach Spielen zurück",
  "VitaOS si apre all'accensione e torna dopo i giochi",
  "Vitaの起動時にVitaOSが開き、ゲーム終了後に戻ります",
  "VitaOS открывается при включении и возвращается после игр"),
 ("Open VitaOS when the Vita starts", "Abrir VitaOS al encender la Vita", "Abrir o VitaOS ao ligar o Vita", "Ouvrir VitaOS au démarrage de la Vita",
  "VitaOS beim Einschalten der Vita öffnen", "Apri VitaOS all'accensione della Vita", "Vitaの起動時にVitaOSを開く", "Открывать VitaOS при включении Vita"),
 ("VitaOS plugin", "Plugin de VitaOS", "Plugin do VitaOS", "Plugin VitaOS", "VitaOS-Plugin", "Plugin di VitaOS", "VitaOSプラグイン", "Плагин VitaOS"),
 ("Installed", "Instalado", "Instalado", "Installé", "Installiert", "Installato", "インストール済み", "Установлен"),
 ("Update ready", "Actualización lista", "Atualização pronta", "Mise à jour prête", "Update bereit", "Aggiornamento pronto", "更新あり", "Есть обновление"),
 ("This Vita has no tai/config.txt, so plugins are not set up here. Install HENkaku/Enso with taiHEN (vita.hacks.guide), then try again.",
  "Esta Vita no tiene tai/config.txt, así que no admite plugins todavía. Instala HENkaku/Enso con taiHEN (vita.hacks.guide) y vuelve a intentarlo.",
  "Este Vita não tem tai/config.txt, então ainda não aceita plugins. Instale HENkaku/Enso com taiHEN (vita.hacks.guide) e tente de novo.",
  "Cette Vita n'a pas de tai/config.txt : les plugins n'y sont pas configurés. Installez HENkaku/Enso avec taiHEN (vita.hacks.guide), puis réessayez.",
  "Diese Vita hat keine tai/config.txt, also sind hier keine Plugins eingerichtet. Installiere HENkaku/Enso mit taiHEN (vita.hacks.guide) und versuche es erneut.",
  "Questa Vita non ha tai/config.txt, quindi i plugin non sono configurati. Installa HENkaku/Enso con taiHEN (vita.hacks.guide) e riprova.",
  "このVitaにはtai/config.txtがなく、プラグインを使えません。taiHEN付きのHENkaku/Ensoを導入してから(vita.hacks.guide)、もう一度お試しください。",
  "На этой Vita нет tai/config.txt, плагины здесь не настроены. Установите HENkaku/Enso с taiHEN (vita.hacks.guide) и повторите."),
 ("VitaOS adds one line to the file below and keeps a copy of the old one. If the Vita starts badly, hold L at power-on to skip all plugins.",
  "VitaOS añade una línea al archivo de abajo y guarda una copia del anterior. Si la Vita arranca mal, mantén L al encender para omitir los plugins.",
  "O VitaOS adiciona uma linha ao arquivo abaixo e guarda uma cópia do antigo. Se o Vita iniciar mal, segure L ao ligar para pular os plugins.",
  "VitaOS ajoute une ligne au fichier ci-dessous et garde une copie de l'ancien. Si la Vita démarre mal, maintenez L à l'allumage pour ignorer les plugins.",
  "VitaOS fügt der Datei unten eine Zeile hinzu und behält eine Kopie der alten. Startet die Vita schlecht, halte L beim Einschalten: Plugins werden übersprungen.",
  "VitaOS aggiunge una riga al file qui sotto e conserva una copia del vecchio. Se la Vita si avvia male, tieni premuto L all'accensione per saltare i plugin.",
  "VitaOSは下のファイルに1行追加し、元のファイルのコピーを残します。うまく起動しないときは、電源を入れる際にLを押し続けるとプラグインを読み込みません。",
  "VitaOS добавит одну строку в файл ниже и сохранит копию старого. Если Vita плохо запускается, держите L при включении: плагины не загрузятся."),
 ("Done. It starts working after a restart. Restart the Vita now?",
  "Listo. Funcionará tras reiniciar. ¿Reiniciar la Vita ahora?", "Pronto. Funciona após reiniciar. Reiniciar o Vita agora?",
  "C'est fait. Actif après un redémarrage. Redémarrer la Vita maintenant ?", "Fertig. Es wirkt nach einem Neustart. Die Vita jetzt neu starten?",
  "Fatto. Funziona dopo un riavvio. Riavviare la Vita ora?", "完了しました。再起動後に有効になります。今すぐVitaを再起動しますか?",
  "Готово. Заработает после перезапуска. Перезапустить Vita сейчас?"),
 ("VitaOS opens at power-on after the next restart", "VitaOS se abrirá al encender tras el próximo reinicio", "O VitaOS abrirá ao ligar após o próximo reinício",
  "VitaOS s'ouvrira au démarrage après le prochain redémarrage", "VitaOS öffnet sich beim Einschalten nach dem nächsten Neustart",
  "VitaOS si aprirà all'accensione dopo il prossimo riavvio", "次回の再起動後から、電源オンでVitaOSが開きます", "VitaOS будет открываться при включении после перезапуска"),
 ("VitaOS opens at power-on", "VitaOS se abre al encender", "O VitaOS abre ao ligar", "VitaOS s'ouvre au démarrage", "VitaOS öffnet sich beim Einschalten",
  "VitaOS si apre all'accensione", "電源オンでVitaOSを開きます", "VitaOS открывается при включении"),
 ("Update the plugin", "Actualizar el plugin", "Atualizar o plugin", "Mettre à jour le plugin", "Plugin aktualisieren", "Aggiorna il plugin", "プラグインを更新", "Обновить плагин"),
 ("Remove the plugin", "Quitar el plugin", "Remover o plugin", "Retirer le plugin", "Plugin entfernen", "Rimuovi il plugin", "プラグインを削除", "Удалить плагин"),
 ("What it does", "Qué hace", "O que ele faz", "Ce qu'il fait", "Was es tut", "Cosa fa", "機能の説明", "Что он делает"),
 ("Plugin updated: restart the Vita to use it", "Plugin actualizado: reinicia la Vita para usarlo", "Plugin atualizado: reinicie o Vita para usar",
  "Plugin mis à jour : redémarrez la Vita pour l'utiliser", "Plugin aktualisiert: Vita neu starten, um es zu nutzen", "Plugin aggiornato: riavvia la Vita per usarlo",
  "プラグインを更新しました。Vitaを再起動すると有効になります", "Плагин обновлён: перезапустите Vita"),
 ("VitaOS will no longer open at power-on or come back after games. The line is taken out of the config; a copy of the old file is kept.",
  "VitaOS ya no se abrirá al encender ni volverá tras los juegos. Se quita la línea de la configuración; se guarda una copia del archivo anterior.",
  "O VitaOS não abrirá mais ao ligar nem voltará após os jogos. A linha sai da configuração; uma cópia do arquivo antigo é guardada.",
  "VitaOS ne s'ouvrira plus au démarrage et ne reviendra plus après les jeux. La ligne est retirée de la configuration ; une copie de l'ancien fichier est gardée.",
  "VitaOS öffnet sich dann nicht mehr beim Einschalten und kehrt nach Spielen nicht zurück. Die Zeile wird aus der Konfiguration entfernt; eine Kopie der alten Datei bleibt.",
  "VitaOS non si aprirà più all'accensione né tornerà dopo i giochi. La riga viene tolta dalla configurazione; resta una copia del vecchio file.",
  "電源オンでVitaOSが開かなくなり、ゲーム終了後にも戻らなくなります。設定ファイルから行を削除し、元のファイルのコピーを残します。",
  "VitaOS больше не будет открываться при включении и возвращаться после игр. Строка убирается из конфигурации; копия старого файла сохраняется."),
 ("Removed: the Vita starts as usual after a restart", "Quitado: la Vita arrancará como siempre tras reiniciar", "Removido: o Vita inicia como sempre após reiniciar",
  "Retiré : la Vita démarrera comme d'habitude après un redémarrage", "Entfernt: Die Vita startet nach einem Neustart wie gewohnt",
  "Rimosso: la Vita si avvia come sempre dopo un riavvio", "削除しました。再起動後は通常どおり起動します", "Удалено: после перезапуска Vita запускается как обычно"),
 ("Opens VitaOS at power-on and after games. Hold L at power-on to skip plugins once.",
  "Abre VitaOS al encender y tras los juegos. Mantén L al encender para omitir los plugins una vez.",
  "Abre o VitaOS ao ligar e após os jogos. Segure L ao ligar para pular os plugins uma vez.",
  "Ouvre VitaOS au démarrage et après les jeux. Maintenez L à l'allumage pour ignorer les plugins une fois.",
  "Öffnet VitaOS beim Einschalten und nach Spielen. Halte L beim Einschalten, um Plugins einmal zu überspringen.",
  "Apre VitaOS all'accensione e dopo i giochi. Tieni premuto L all'accensione per saltare i plugin una volta.",
  "電源オン時とゲーム終了後にVitaOSを開きます。電源を入れる際にLを押し続けると、その回だけプラグインを読み込みません。",
  "Открывает VitaOS при включении и после игр. Держите L при включении, чтобы один раз пропустить плагины."),
 ("Your old config is kept as config.txt.before-vitaos, next to:",
  "La configuración anterior queda en config.txt.before-vitaos, junto a:", "A configuração antiga fica em config.txt.before-vitaos, junto de:",
  "Ancienne configuration : config.txt.before-vitaos, à côté de :", "Alte Konfiguration: config.txt.before-vitaos, neben:",
  "Vecchia configurazione: config.txt.before-vitaos, accanto a:", "元の設定は config.txt.before-vitaos として次の場所に残ります:",
  "Старая конфигурация: config.txt.before-vitaos, рядом с:"),
 ("This config file is saved as UTF-16, which VitaOS will not edit. Nothing was changed: add the plugin line by hand.",
  "Este archivo de configuración está guardado en UTF-16 y VitaOS no lo edita. No se cambió nada: añade la línea del plugin a mano.",
  "Este arquivo de configuração está salvo em UTF-16 e o VitaOS não o edita. Nada foi alterado: adicione a linha do plugin à mão.",
  "Ce fichier de configuration est enregistré en UTF-16 ; VitaOS ne le modifie pas. Rien n'a été changé : ajoutez la ligne du plugin à la main.",
  "Diese Konfigurationsdatei ist als UTF-16 gespeichert; VitaOS bearbeitet sie nicht. Nichts wurde geändert: Füge die Plugin-Zeile von Hand hinzu.",
  "Questo file di configurazione è salvato in UTF-16 e VitaOS non lo modifica. Non è stato cambiato nulla: aggiungi la riga del plugin a mano.",
  "この設定ファイルはUTF-16で保存されているため、VitaOSでは編集しません。何も変更していません。プラグインの行を手動で追加してください。",
  "Этот файл конфигурации сохранён в UTF-16, VitaOS его не редактирует. Ничего не изменено: добавьте строку плагина вручную."),
 ("This config file contains a stray zero byte, so VitaOS will not edit it. Nothing was changed: add the plugin line by hand.",
  "Este archivo de configuración contiene un byte cero suelto, así que VitaOS no lo edita. No se cambió nada: añade la línea del plugin a mano.",
  "Este arquivo de configuração contém um byte zero perdido, então o VitaOS não o edita. Nada foi alterado: adicione a linha do plugin à mão.",
  "Ce fichier de configuration contient un octet nul isolé ; VitaOS ne le modifie pas. Rien n'a été changé : ajoutez la ligne du plugin à la main.",
  "Diese Konfigurationsdatei enthält ein einzelnes Nullbyte, daher bearbeitet VitaOS sie nicht. Nichts wurde geändert: Füge die Plugin-Zeile von Hand hinzu.",
  "Questo file di configurazione contiene un byte zero isolato, quindi VitaOS non lo modifica. Non è stato cambiato nulla: aggiungi la riga del plugin a mano.",
  "この設定ファイルには不要なゼロバイトが含まれているため、VitaOSでは編集しません。何も変更していません。プラグインの行を手動で追加してください。",
  "В этом файле конфигурации есть лишний нулевой байт, поэтому VitaOS его не редактирует. Ничего не изменено: добавьте строку плагина вручную."),
 ("Could not keep a backup of the config file, so nothing was changed.",
  "No se pudo guardar una copia del archivo de configuración, así que no se cambió nada.", "Não foi possível guardar uma cópia do arquivo de configuração, então nada foi alterado.",
  "Impossible de garder une copie du fichier de configuration : rien n'a été changé.", "Es konnte keine Kopie der Konfigurationsdatei angelegt werden; nichts wurde geändert.",
  "Impossibile conservare una copia del file di configurazione: non è stato cambiato nulla.", "設定ファイルのコピーを保存できなかったため、何も変更していません。",
  "Не удалось сохранить копию файла конфигурации, ничего не изменено."),
 ("Could not write the new config file, so nothing was changed.",
  "No se pudo escribir el nuevo archivo de configuración, así que no se cambió nada.", "Não foi possível gravar o novo arquivo de configuração, então nada foi alterado.",
  "Impossible d'écrire le nouveau fichier de configuration : rien n'a été changé.", "Die neue Konfigurationsdatei konnte nicht geschrieben werden; nichts wurde geändert.",
  "Impossibile scrivere il nuovo file di configurazione: non è stato cambiato nulla.", "新しい設定ファイルを書き込めなかったため、何も変更していません。",
  "Не удалось записать новый файл конфигурации, ничего не изменено."),
 ("Could not put the config file back. It is safe under the name below: rename it to config.txt in VitaShell before restarting.",
  "No se pudo restaurar el archivo de configuración. Está a salvo con el nombre de abajo: renómbralo a config.txt en VitaShell antes de reiniciar.",
  "Não foi possível restaurar o arquivo de configuração. Ele está a salvo com o nome abaixo: renomeie para config.txt no VitaShell antes de reiniciar.",
  "Impossible de remettre le fichier de configuration. Il est en sécurité sous le nom ci-dessous : renommez-le en config.txt dans VitaShell avant de redémarrer.",
  "Die Konfigurationsdatei konnte nicht zurückgelegt werden. Sie liegt sicher unter dem Namen unten: Benenne sie in VitaShell in config.txt um, bevor du neu startest.",
  "Impossibile ripristinare il file di configurazione. È al sicuro con il nome qui sotto: rinominalo in config.txt con VitaShell prima di riavviare.",
  "設定ファイルを元に戻せませんでした。下の名前で安全に残っています。再起動する前に、VitaShellで config.txt に名前を変更してください。",
  "Не удалось вернуть файл конфигурации. Он в сохранности под именем ниже: переименуйте его в config.txt в VitaShell до перезапуска."),
 ("Could not replace the config file; the old one is back in place.",
  "No se pudo reemplazar el archivo de configuración; el anterior vuelve a estar en su sitio.", "Não foi possível substituir o arquivo de configuração; o antigo está de volta.",
  "Impossible de remplacer le fichier de configuration ; l'ancien est de nouveau en place.", "Die Konfigurationsdatei konnte nicht ersetzt werden; die alte ist wieder an ihrem Platz.",
  "Impossibile sostituire il file di configurazione; il vecchio è di nuovo al suo posto.", "設定ファイルを置き換えられませんでした。元のファイルに戻してあります。",
  "Не удалось заменить файл конфигурации; старый возвращён на место."),
 ("Could not read the config file.", "No se pudo leer el archivo de configuración.", "Não foi possível ler o arquivo de configuração.", "Impossible de lire le fichier de configuration.",
  "Die Konfigurationsdatei konnte nicht gelesen werden.", "Impossibile leggere il file di configurazione.", "設定ファイルを読み込めませんでした。", "Не удалось прочитать файл конфигурации."),
 ("This VitaOS package has no plugin inside.", "Este paquete de VitaOS no incluye el plugin.", "Este pacote do VitaOS não traz o plugin.", "Ce paquet VitaOS ne contient pas le plugin.",
  "Dieses VitaOS-Paket enthält kein Plugin.", "Questo pacchetto di VitaOS non contiene il plugin.", "このVitaOSパッケージにはプラグインが入っていません。", "В этом пакете VitaOS нет плагина."),
 ("Could not copy the plugin to ur0:tai.", "No se pudo copiar el plugin a ur0:tai.", "Não foi possível copiar o plugin para ur0:tai.", "Impossible de copier le plugin dans ur0:tai.",
  "Das Plugin konnte nicht nach ur0:tai kopiert werden.", "Impossibile copiare il plugin in ur0:tai.", "プラグインを ur0:tai にコピーできませんでした。", "Не удалось скопировать плагин в ur0:tai."),
 ("Show in Play", "Mostrar en Jugar", "Mostrar em Jogar", "Afficher dans Jouer", "In Spielen zeigen", "Mostra in Gioca", "プレイに表示", "Показать в Играх"),
 ("Moved to Play", "Movido a Jugar", "Movido para Jogar", "Déplacé dans Jouer", "Nach Spielen verschoben", "Spostato in Gioca", "プレイに移動しました", "Перемещено в Игры"),
 ("Moved to Apps", "Movido a Apps", "Movido para Apps", "Déplacé dans Apps", "Nach Apps verschoben", "Spostato in App", "アプリに移動しました", "Перемещено в Прил."),
 ("X Move it to Apps", "X Mover a Apps", "X Mover para Apps", "X Déplacer dans Apps", "X Nach Apps verschieben", "X Sposta in App", "X アプリに移動", "X Переместить в Прил."),
 ("Could not save that: the card may be full", "No se pudo guardar: la tarjeta puede estar llena", "Não foi possível salvar: o cartão pode estar cheio",
  "Impossible d'enregistrer : la carte est peut-être pleine", "Konnte nicht gespeichert werden: Die Karte ist vielleicht voll",
  "Impossibile salvare: la scheda potrebbe essere piena", "保存できませんでした。カードの空きがない可能性があります", "Не удалось сохранить: возможно, карта заполнена"),
 ("The emulator for this game is not installed any more. Install it from the Store, then try again.",
  "El emulador de este juego ya no está instalado. Instálalo desde la Tienda y vuelve a intentarlo.", "O emulador deste jogo não está mais instalado. Instale-o pela Loja e tente de novo.",
  "L'émulateur de ce jeu n'est plus installé. Installez-le depuis la Boutique, puis réessayez.", "Der Emulator für dieses Spiel ist nicht mehr installiert. Installiere ihn aus dem Store und versuche es erneut.",
  "L'emulatore di questo gioco non è più installato. Installalo dallo Store e riprova.", "このゲームのエミュレーターがインストールされていません。ストアからインストールして、もう一度お試しください。",
  "Эмулятор для этой игры больше не установлен. Установите его из Магазина и повторите."),
 ("No games yet", "Aún no hay juegos", "Ainda não há jogos", "Pas encore de jeux", "Noch keine Spiele", "Ancora nessun gioco", "まだゲームがありません", "Игр пока нет"),
 ("VitaOS shows the PS Vita games you have installed, PSP games in ux0:pspemu, and ROMs in ux0:roms/<console> (for example ux0:roms/snes) once RetroArch is installed.",
  "VitaOS muestra los juegos de PS Vita instalados, los de PSP en ux0:pspemu y las ROM en ux0:roms/<consola> (por ejemplo ux0:roms/snes) cuando RetroArch está instalado.",
  "O VitaOS mostra os jogos de PS Vita instalados, os de PSP em ux0:pspemu e as ROMs em ux0:roms/<console> (por exemplo ux0:roms/snes) quando o RetroArch está instalado.",
  "VitaOS affiche les jeux PS Vita installés, les jeux PSP dans ux0:pspemu et les ROM dans ux0:roms/<console> (par exemple ux0:roms/snes) une fois RetroArch installé.",
  "VitaOS zeigt installierte PS-Vita-Spiele, PSP-Spiele in ux0:pspemu und ROMs in ux0:roms/<Konsole> (zum Beispiel ux0:roms/snes), sobald RetroArch installiert ist.",
  "VitaOS mostra i giochi PS Vita installati, i giochi PSP in ux0:pspemu e le ROM in ux0:roms/<console> (ad esempio ux0:roms/snes) quando RetroArch è installato.",
  "VitaOSは、インストール済みのPS Vitaゲーム、ux0:pspemu のPSPゲーム、そしてRetroArchを入れると ux0:roms/<機種名>(例: ux0:roms/snes)のROMを表示します。",
  "VitaOS показывает установленные игры PS Vita, игры PSP из ux0:pspemu и ROM-файлы из ux0:roms/<консоль> (например, ux0:roms/snes), когда установлен RetroArch."),
 ("Press R to reach the Store tab for RetroArch and other emulators, copy your games over, then choose Settings > VitaOS > Find games again.",
  "Pulsa R hasta la pestaña Tienda para instalar RetroArch y otros emuladores, copia tus juegos y elige Ajustes > VitaOS > Buscar juegos de nuevo.",
  "Aperte R até a aba Loja para instalar o RetroArch e outros emuladores, copie seus jogos e escolha Ajustes > VitaOS > Procurar jogos de novo.",
  "Appuyez sur R jusqu'à l'onglet Boutique pour installer RetroArch et d'autres émulateurs, copiez vos jeux, puis choisissez Réglages > VitaOS > Rechercher les jeux.",
  "Drücke R bis zum Tab Store, um RetroArch und andere Emulatoren zu installieren, kopiere deine Spiele und wähle dann Optionen > VitaOS > Spiele neu suchen.",
  "Premi R fino alla scheda Store per installare RetroArch e altri emulatori, copia i tuoi giochi, poi scegli Impostaz. > VitaOS > Cerca di nuovo i giochi.",
  "Rボタンでストアのタブに移動してRetroArchなどのエミュレーターを入れ、ゲームをコピーしてから、設定 > VitaOS > ゲームを再検索 を選んでください。",
  "Нажимайте R до вкладки Магазин, установите RetroArch и другие эмуляторы, скопируйте игры и выберите Настройки > VitaOS > Найти игры заново."),
 ("Welcome to VitaOS", "Te damos la bienvenida a VitaOS", "Boas-vindas ao VitaOS", "Bienvenue dans VitaOS", "Willkommen bei VitaOS", "Ti diamo il benvenuto in VitaOS", "VitaOSへようこそ", "Добро пожаловать в VitaOS"),
 ("L and R switch tabs. Games are in Play, homebrew is in the Store.",
  "L y R cambian de pestaña. Los juegos están en Jugar; el homebrew, en la Tienda.", "L e R trocam de aba. Os jogos ficam em Jogar; o homebrew, na Loja.",
  "L et R changent d'onglet. Les jeux sont dans Jouer, le homebrew dans la Boutique.", "L und R wechseln die Tabs. Spiele sind unter Spielen, Homebrew im Store.",
  "L e R cambiano scheda. I giochi sono in Gioca, l'homebrew nello Store.", "LとRでタブを切り替えます。ゲームはプレイ、自作ソフトはストアにあります。",
  "L и R переключают вкладки. Игры — в «Игры», homebrew — в Магазине."),
 ("To open VitaOS when the Vita starts: Settings > VitaOS > Start at power-on.",
  "Para abrir VitaOS al encender la Vita: Ajustes > VitaOS > Abrir al encender.", "Para abrir o VitaOS ao ligar o Vita: Ajustes > VitaOS > Abrir ao ligar.",
  "Pour ouvrir VitaOS au démarrage de la Vita : Réglages > VitaOS > Ouvrir au démarrage.", "VitaOS beim Einschalten öffnen: Optionen > VitaOS > Beim Einschalten öffnen.",
  "Per aprire VitaOS all'accensione: Impostaz. > VitaOS > Apri all'accensione.", "Vitaの起動時にVitaOSを開くには: 設定 > VitaOS > 電源オンで開く",
  "Чтобы VitaOS открывалась при включении: Настройки > VitaOS > Открывать при включении."),
 ("That song would not play: it needs to be an AAC .m4a", "Esta canción no se reprodujo: debe ser un .m4a en AAC", "Esta música não tocou: precisa ser um .m4a em AAC",
  "Ce titre ne peut pas être lu : il faut un .m4a en AAC", "Dieser Song ließ sich nicht abspielen: Er muss ein AAC-.m4a sein",
  "Questo brano non si riproduce: deve essere un .m4a in AAC", "この曲は再生できません。AAC形式の .m4a が必要です", "Песня не воспроизводится: нужен файл .m4a в формате AAC"),
 ("That film would not play: it needs to be an H.264 .mp4 up to 960x544", "Esta película no se reprodujo: debe ser un .mp4 H.264 de hasta 960x544",
  "Este filme não tocou: precisa ser um .mp4 H.264 de até 960x544", "Ce film ne peut pas être lu : il faut un .mp4 H.264 jusqu'à 960x544",
  "Dieser Film ließ sich nicht abspielen: Er muss ein H.264-.mp4 bis 960x544 sein", "Questo film non si riproduce: deve essere un .mp4 H.264 fino a 960x544",
  "この映画は再生できません。960x544までのH.264の .mp4 が必要です", "Фильм не воспроизводится: нужен .mp4 H.264 до 960x544"),
 ("Apple Lossless (ALAC) can't play on the Vita: convert it to AAC", "Apple Lossless (ALAC) no se reproduce en la Vita: conviértelo a AAC",
  "Apple Lossless (ALAC) não toca no Vita: converta para AAC", "Apple Lossless (ALAC) n'est pas lisible sur la Vita : convertissez en AAC",
  "Apple Lossless (ALAC) läuft auf der Vita nicht: in AAC umwandeln", "Apple Lossless (ALAC) non si riproduce sulla Vita: convertilo in AAC",
  "Apple Lossless (ALAC) はVitaで再生できません。AACに変換してください", "Apple Lossless (ALAC) не воспроизводится на Vita: преобразуйте в AAC"),
 ("This file's sound format can't play on the Vita: convert it to AAC", "El formato de sonido de este archivo no se reproduce en la Vita: conviértelo a AAC",
  "O formato de som deste arquivo não toca no Vita: converta para AAC", "Le format audio de ce fichier n'est pas lisible sur la Vita : convertissez en AAC",
  "Das Tonformat dieser Datei läuft auf der Vita nicht: in AAC umwandeln", "Il formato audio di questo file non si riproduce sulla Vita: convertilo in AAC",
  "このファイルの音声形式はVitaで再生できません。AACに変換してください", "Формат звука этого файла не воспроизводится на Vita: преобразуйте в AAC"),
 ("VitaOS's player module did not load: reinstall VitaOS", "El módulo reproductor de VitaOS no se cargó: reinstala VitaOS", "O módulo reprodutor do VitaOS não carregou: reinstale o VitaOS",
  "Le module de lecture de VitaOS ne s'est pas chargé : réinstallez VitaOS", "Das Player-Modul von VitaOS wurde nicht geladen: VitaOS neu installieren",
  "Il modulo di riproduzione di VitaOS non si è caricato: reinstalla VitaOS", "VitaOSの再生モジュールを読み込めませんでした。VitaOSを再インストールしてください",
  "Модуль плеера VitaOS не загрузился: переустановите VitaOS"),
]

def main():
    root = Path(__file__).resolve().parents[1] / "assets" / "lang"
    check = "--check" in sys.argv
    problems = 0
    for li, lang in enumerate(LANGS):
        path = root / (lang + ".tsv")
        lines = path.read_text(encoding="utf-8").split("\n")
        have = {l.split("\t")[0] for l in lines if "\t" in l and not l.startswith("#")}
        missing = [t for t in T if t[0] not in have]
        dead = [d for d in DEAD if d in have]
        if check:
            if missing or dead:
                print("%s: %d missing, %d dead" % (lang, len(missing), len(dead))); problems += 1
            continue
        ours = {t[0] for t in T}
        lines = [l for l in lines if l.split("\t")[0] not in DEAD and l.split("\t")[0] not in ours]   # ours are rewritten below
        missing = T
        while lines and lines[-1] == "":
            lines.pop()
        for t in missing:
            assert "\t" not in t[li + 1] and "\n" not in t[li + 1]
            lines.append("%s\t%s" % (t[0], t[li + 1]))
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print("%s: +%d, -%d" % (lang, len(missing), len(dead)))
    if check:
        try:                                  # every character must exist in a font VitaOS ships
            from fontTools.ttLib import TTFont
            assets = root.parent
            inter = set(TTFont(assets / "Inter-Regular.ttf").getBestCmap())
            jp = set(TTFont(assets / "NotoSansJP-Subset.otf").getBestCmap())
            for li, lang in enumerate(LANGS):
                bad = sorted({c for t in T for c in t[li + 1] if ord(c) not in inter and ord(c) not in jp})
                if bad:
                    print("%s: no glyph for %s" % (lang, " ".join(bad))); problems += 1
        except ImportError:
            print("(fontTools not installed: glyph check skipped)")
        print("OK" if not problems else "PROBLEMS")
    return 1 if problems and check else 0

if __name__ == "__main__":
    sys.exit(main())
