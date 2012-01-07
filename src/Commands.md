# Projet METAL

## Commands
List of commands supported by the VM

### Native
| famille | label | type | commentaire |
|   -- |  -- | -- | -- | -- |
| core | hd | fun[list u0]u0 | premier élément d'une liste |
| core | tl | fun[list u0]list u0 | liste privée de son premier élément |
| core | tabnew | fun[u0 I]tab u0 | création d'une table : valeur d'initialisation, taille de la table |
| core | tablen | fun[tab u0]I | taille d'une table |
| core | abs | fun[I]I | valeur absolue |
| core | min | fun[I I]I | minimum |
| core | max | fun[I I]I | maximum |
| core | rand | fun[]I | nombre aléatoire sur 16 bits |
| core | srand | fun[I]I | définition de la graine du générateur aléatoire |
| core | strnew | fun[I]S | création d'une nouvelle chaîne de caractères, dont la taille est passée en argument |
| core | strset | fun[S I I]S | changement d'un caractère d'une chaîne : chaine, index, valeur |
| core | strcpy | fun[S I S I I]S | copie d'une sous-chaîne de caractère : chaîne destination, index destination, chaîne source, index source, longeur |
| core | vstrcmp | fun[S I S I I]I | comparaison d'une sous-chaîne de caractère : chaîne destination, index destination, chaîne source, index source, longeur |
| core | strfind | fun[S I S I I]I | recherche d'une sous-chaîne dans une chaîne : chaîne, index, sous-chaîne, index, taille ; retourne l'index dans la chaîne (nil si non trouvé) |
| custom| strstr | fun[S S I]I | fin substring string, needle, start; returns pos
| core | strfindrev | fun[S I S I I]I | recherche inversée d'une sous-chaîne dans une chaîne : chaîne, index, sous-chaîne, index, taille ; retourne l'index dans la chaîne (nil si non trouvé) |
| core | strlen | fun[S]I | taille d'une chaîne de caractères |
| core | strget | fun[S I]I | retourne le n-ième caractères d'une chaîne (entier entre 0 et 255) |
| core | strsub | fun[S I I]S | calcul d'une sous-chaîne : chaîne source, index source, taille |
| core | strcat | fun[S S]S | concaténation de deux chaînes |
| core | strcatlist | fun[list S]S | concaténation d'une liste de chaînes |
| custom| listtostr | fun[list I]S | integer list to string
| core | atoi | fun[S]I | Conversion d'une chaîne en base 10 vers un entier |
| core | htoi | fun[S]I | Conversion d'une chaîne hexadécimale vers un entier |
| core | itoa | fun[I]S | Conversion d'un entier vers une chaîne en base 10 |
| core | ctoa | fun[I]S | Conversion d'un code ascii vers une chaîne d'un seul caractère |
| core | itoh | fun[I]S | Conversion d'un entier vers une chaîne en hexadécimal |
| core | ctoh | fun[I]S | Conversion d'un caractère vers une chaîne en hexadécimal de taille 2 |
| core | itobin2 | fun[I]S | Conversion d'un entier vers une chaîne de deux caractères |
| core | strcmp | fun[S S]I | Comparaison de deux chaînes de caractères |
| core | crypt | fun[S I I I I]I | Cryptage simple : chaîne, index, taille, clef, delta ; retourne la nouvelle clef |
| core | uncrypt | fun[S I I I I]I | Décryptage simple : chaîne, index, taille, clef, delta ; retourne la nouvelle clef |
| core | listswitch | fun[list[u0 u1] u0]u1 | Recherche d'un élément quelconque dans une liste d'associations |
| core | listswitchstr | fun[list[S u1] S]u1 | Recherche d'une chaîne dans une liste d'associations |
| device | led | fun[I I]I | définition de la couleur d'une led : numéro de la led, couleur rgb 24 bits |
| device | motorset | fun[I I]I | réglage d'un moteur : index moteur, direction |
| device | motorget | fun[I]I | lecture de la position d'un moteur : index moteur |
| device | button2 | fun[]I | lecture de l'état du bouton de tête : 0=relâché |
| device | button3 | fun[]I | lecture de l'état de la molette : 0 = position maxi, 255= position butée |
| device | load | fun[S I S I I]I | lecture de la flash : chaîne destination, index, chaîne source (="conf.bin"), index source (=0), taille |
| device | save | fun[S I S I I]I | sauvegarde dans la flash : chaîne source, index source, chaîne destination (="conf.bin"), index destination (=0), taille |
| device | loopcb | fun[fun[]I]fun[]I | définition de la callback principale (appelée 20 fois par seconde) |
| device | rfidGet | fun[]S | lecture du tag rfid : retourne "nil" si aucun, "ERROR" si erreur, id de tag sinon |
| device | reboot | fun[I I]I | Force le reboot : deux valeurs magic 0x0407FE58 0x13fb6754 |
| device | flashFirmware | fun[S I I]I | Flashage du firmware : chaîne, deux valeurs magic 0x13fb6754 0x0407FE58 |
| hack | gc | fun[]I | provocation du gc |
| hack | corePP | fun[]I | Retourne le pointeur de pile |
| hack | corePush | fun[u0]u0 | Forcer l'empilement d'une valeur |
| hack | corePull | fun[I]I | Forcer le dépilement d'une valeur |
| hack | coreBit0 | fun[u0 I]u0 | Modifie le bit 0 de la valeur située au sommet de la pile |
| net | netCb | fun[fun[S S]I]fun[S S]I | Définition de la callback réseau, dont les arguments sont : trame, mac émetteur |
| net | netSend | fun[S I I S I I]I | Envoi d'une trame réseau : chaîne source, index, taille, mac destination, xxx,xxx |
| net | netState | fun[]I | Retourne l'état de la connexion |
| net | netMac | fun[]S | Retourne l'adresse mac de l'hôte (chaîne de 6 caractères) |
| net | netChk | fun[S I I I]I | Calcul le checksum IP : chaîne, index, taille, checksum de départ |
| net | netSetmode | fun[I S I]I | Demande la passage dans un mode réseau particulier |
| net | netScan | fun[S]list[S S S I I I I] | Effectue un scan réseau : SSID cherché (nil : tous) ; retourne une liste [ssid mac bssid rssi channel rateset encryption] |
| net | netAuth | fun[[S S S I I I I] I I S][S S S I I I I] | Procédure d'authentification : résultat du scan, mode d'authentification, style d'authentification, clef |
| net | netPmk | fun[S S]S | Calcul de la clef Pmk (Wpa) : ssid, clef wpa |
| net | netRssi | fun[]I | Retourne le Rssi moyen (puissance de réception) |
| net | netSeqAdd | fun[S I]S | Mise à jour d'un numéro de séquence TCP : numéro initial, taille paquet |
| net | strgetword | fun[S I]I | Lit un mot 16 bits dans un header IP : chaîne source, index |
| net | strputword | fun[S I I]S | Ecrit un mot 16 bits dans un header IP : chaîne source, index, valeur |
| simu | udpStart | fun[I]I | Démarre un serveur udp : port udp ; retourne un id de socket |
| simu | udpCb | fun[fun[I S S]I]fun[I S S]I | Définit la callback udp, dont les arguments sont : id de socket, trame reçue, ip émetteur |
| simu | udpStop | fun[I]I | Stoppe un serveur udp : port udp |
| simu | udpSend | fun[I S I S I I]I | Envoi d'une trame udp : id de socket, ip destinataire, port destinataire, chaîne, index, longueur |
| simu | tcpOpen | fun[S I]I | Crée une connexion tcp : ip destinataire, port destinataire ; retourne un id de socket |
| simu | tcpClose | fun[I]I | Ferme une connexion tcp : id de socket |
| simu | tcpSend | fun[I S I I]I | Envoi d'une trame tcp : id de socket, ip destinataire, port destinataire, chaîne, index, longueur ; retourne l'index du prochain octet à transmettre |
| simu | tcpCb | fun[fun[I I S]I]fun[I I S]I | Définit la callback tcp, dont les arguments sont : id de socket, événement(-1 : erreur/fin, 2 : accept (dans ce cas, le troisième argument est l'id de la nouvelle socket), 0 : write, 1 : read), trame reçue |
| simu | tcpListen | fun[I]I | Démarre un serveur tcp : port tcp ; retourne un id de socket |
| simu | tcpEnable | fun[I I]I | Place une socket tcp en pause : id de socket, mode (1=pause, 0=normal) |
| sound | playStart | fun[I fun[I]I]I | lancement du player audio : inutilisé, callback de remplissage du buffer audio (paramètre : nombre d'octets attendus) |
| sound | playFeed | fun[S I I]I | remplissage du buffer audio : chaîne, index, taille. Retourne le nombre d'octets copiés dans le buffer |
| sound | playStop | fun[]I | arrêt du player audio |
| sound | recStart | fun[I I fun[S]I]I | lancement de l'enregistreur audio : fréquence (8000), gain (0=automatique), callback (paramètre : échantillon enregistré) |
| sound | recStop | fun[]I | arrêt de l'enregistreur audio |
| sound | recVol | fun[S I]I | calcul du volume : chaîne, index |
| sound | sndVol | fun[I]I | définition du volume du player audio |
| sound | playTime | fun[]I | retourne le temps de décodage |
| sound | sndRefresh | fun[]I | force l'appel à la routine de gestion du chip audio |
| sound | sndWrite | fun[I I]I | Ecrit un registre du chip audio : numéro de registre, valeur |
| sound | sndRead | fun[I]I | Lit un registre du chip audio : numéro de registre |
| sound | sndFeed | fun[S I I]I | Ecrit directement dans le buffer du chip audio : chaîne, index, taille |
| sound | sndAmpli | fun[I]I | Définit l'état de l'ampli : 1=on, 0=off |
| sound | adp2wav | fun[S I S I I]S | Conversion adp vers wav : chaîne destination, index destination, chaîne source, index source, longueur |
| sound | wav2adp | fun[S I S I I]S | Conversion wav vers adp : chaîne destination, index destination, chaîne source, index source, longueur |
| sound | alaw2wav | fun[S I S I I I]S | Conversion alaw/mulaw vers wav : chaîne destination, index destination, chaîne source, index source, longueur, type (xxx) |
| sound | wav2alaw | fun[S I S I I I]S | Conversion alaw/mulaw vers wav : chaîne destination, index destination, chaîne source, index source, longueur, type (xxx) |
| sys | Secholn | fun[S]S | affichage d'une chaîne de caractères sur la sortie standard, suivie d'un retour à la ligne |
| custom | SLecho | fun[list S]S | echo string list
| custom | SLecholn | fun[list S]S | echo string list
| custom | ILecho | fun[list I]S | echo string list
| custom | ILecholn | fun[list I]S | echo string list
| sys | Secho | fun[S]S | affichage d'une chaîne de caractères sur la sortie standard |
| sys | Iecholn | fun[u0]u0 | affichage d'un entier en décimal sur la sortie standard, suivie d'un retour à la ligne |
| sys | Iecho | fun[u0]u0 | affichage d'un entier en décimal sur la sortie standard |
| sys | time | fun[]I | heure en secondes |
| sys | time_ms | fun[]I | heure en millisecondes |
| sys | bytecode | fun[S]S | chargement d'un nouveau bytecode |
| sys | envget | fun[]S | lecture de l'environnement |
| sys | envset | fun[S]S | réglage de l'environnement (limité à 4096 octets) |
| device | rfidGetList | fun[] list S | lecture d'une liste de tags rfid |
| device | rfidRead | fun[S I]S | lecture d'un bloc de données d'un tag : id du tag, numéro du bloc ; retourne les données (chaîne de 4 octets binaires) |
| device | rfidWrite | fun[S I S]I | écriture d'un bloc de données d'un tag : id du tag, numéro du bloc, donnée à écrire (chaîne de 4 octets) ; retourne 0 si ok |

## Bytecode

| label | opcode | commentaire |
| -- | -- | -- |
| exec | 0 | appel d'une routine |
| ret | 1 | fin d'une routine |
| intb | 2 | empiler un entier 8 bits |
| int | 3 | empiler un entier |
| nil | 4 | empiler "nil" |
| drop | 5 | dépiler un élément |
| dup | 6 | dupliquer un élément dans la pile |
| getlocalb | 7 | empiler une variable locale, dont l'index tient sur 8 bits |
| getlocal | 8 | empiler une variable locale, dont l'index au sommet de la pile |
| add | 9 | ajouter les deux valeurs au sommet de la pile |
| sub | 10 | soustraire les deux valeurs au sommet de la pile |
| mul | 11 | multiplier les deux valeurs au sommet de la pile |
| div | 12 | diviser les deux valeurs au sommet de la pile |
| mod | 13 | modulo des deux valeurs au sommet de la pile |
| and | 14 | ET logique entre les bits des deux valeurs au sommet de la pile |
| or | 15 | OU logique entre les bits des deux valeurs au sommet de la pile |
| eor | 16 | OU EXCLUSIF logique entre les bits des deux valeurs au sommet de la pile |
| shl | 17 | décalage vers la gauche |
| shr | 18 | décalage vers la droite |
| neg | 19 | changement de signe de la valeur au sommet de la pile |
| not | 20 | inversion des bits de la valeur au sommet de la pile |
| non | 21 | inversion booléenne |
| eq | 22 | test d'égalité des deux valeurs au sommet de la pile |
| ne | 23 | test de différence des deux valeurs au sommet de la pile |
| lt | 24 | test d'infériorité des deux valeurs au sommet de la pile |
| gt | 25 | test de supériorité des deux valeurs au sommet de la pile |
| le | 26 | test "inférieur ou égal" des deux valeurs au sommet de la pile |
| ge | 27 | test "supérieur ou égal" des deux valeurs au sommet de la pile |
| goto | 28 | déplacement du pointeur programme |
| else | 29 | déplacement conditionnel du pointeur programme en fonction de la valeur au sommet de la pile |
| mktabb | 30 | crée une table dont la taille est constante et tient sur 8 bits |
| mktab | 31 | crée une table dont la taille est au sommet de la pile |
| deftabb | 32 | crée une table dont la taille est constante et tient sur 8 bits, en initialisant les valeurs à partir de la pile |
| deftab | 33 | crée une table dont la taille est au sommet de la pile, en initialisant les valeurs à partir de la pile |
| fetchb | 34 | empile un élément d'un tuple, dont l'index est constant et tient sur 8 bits |
| fetch | 35 | empile un élément d'un tuple, dont l'index est au sommet de la pile |
| getglobalb | 36 | empiler une variable globale, dont l'index tient sur 8 bits |
| getglobal | 37 | empiler une variable globale, dont l'index au sommet de la pile |
| Secho | 38 | voir lib native |
| Iecho | 39 | voir lib native |
| setlocalb | 40 | écrit une variable locale, dont l'index tient sur 8 bits |
| setlocal | 41 | écrit une variable locale, dont l'index est dans la pile |
| setglobal | 42 | écrit une variable globale |
| setstructb | 43 | écrit un élément d'un tuple, dont l'index est constant et tient sur 8 bits |
| setstruct | 44 | écrit un élément d'un tuple, dont l'index est donné dans la pile |
| hd | 45 | voir lib native |
| tl | 46 | voir lib native |
| setlocal2 | 47 | écrit une variable locale, dont l'index est dans la pile |
| store | 48 | écrit un élément d'un tuple, dont l'index est donné dans la pile |
| call | 49 | appel d'une routine |
| callrb | 50 | appel d'une routine |
| callr | 51 | appel d'une routine |
| first | 52 | empile le premier élément d'un tuple |
