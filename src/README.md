# Projet METAL

## Grammaire

Auteur: Sylvain Huet
Création: 13/01/03
Dernière mise-à-jour: 20/02/07


## Introduction
### Aperçu sur le document
Grammaire du langage Métal.

## Description
### Types

    Type  =  B |  B( labels* )
     |  un |  wn
     |  rn
     |  listType |  tabType |  [Type*]
     |  fun [Type*] Type
    TypeMono  =  B |  B( labels* )
     |  wn |  rn
     |  listTypeMono |  tabTypeMono |  [TypeMono*]
     |  fun [TypeMono*] TypeMono

B  =  Type de base
un  =  variable liée
rn  =  récursion de niveau n

Les types de base sont:

      I:  int
      S:  string
      F:  float
      Env:  environnement

Cette liste n’est pas exhaustive: grâce aux structures et aux constructeurs de types, vous pouvez vous-même développer vos types de base.

Quelques commentaires sur le tableau, si vous n’êtes pas familier avec ces notations.
La première ligne définit l’expression Type, qui est en fait le type Scol. Puis, séparés par des ‘|’, on trouve les différentes manières d’écrire l’expression: cela peut être:

  - B, qui est défini à la troisième ligne: c’est un type de base, tel que I, S, F, ... Donc puisque I est un type de base, B peut s’écrire I, et puisque Type peut s’écrire B, I est bien un type en Métal.
  - un avec n entier: u0, u1, u2, u3, ... sont des types: ils correspondent aux variables liées.
  - wn avec n entier: w0, w1, w2, w3, ... sont des types faibles.
  - rn avec n entier: r0, r1, r2, r3, ... sont des types: ils définissent les récursions dans les types.
  - tab Type: type tableau. Le mot tab est suivi du type des éléments du tableau. Par exemple tab I est le type d’un tableau d’entiers.
  - list Type: type tableau. Le mot list est suivi du type des éléments de la liste. Par exemple list I est le type d’une liste d’entiers.
  - [Type*]: type tuple. Entre crochets, on écrit plusieurs Types (c’est le sens de l’étoile *). Par exemple, [I S] est un tuple de deux éléments dont le premier est un entier et le second une chaîne de caractères. Eventuellement, le tuple est vide: [ ]. Le tuple peut lui-même contenir des tuples: [I [S I]]
  - fun [Type*] Type: type fonction. le mot fun est suivi d’un tuple contenant les arguments de la fonction puis du type du résultat. Par exemple ‘fun [I I] S‘ est une fonction qui prend deux entiers en arguments, et retourne une chaîne de caractères.

L’expression TypeMono définit les types monomorphes (non polymorphes): la seule différence avec Type est l’absence des variables liées un.


### Sources

Metal  =  Definition*
Definition  =  fun Function Args = Program ;;
 |  var Var( = Val) ;;
 |  proto Function Nbargs ;;
 |  proto Function = Type ;;
 |  type TypeName ;;
 |  type TypeName = [ Fields ] ;;
 |  type TypeName = TypeConstr ;;


Program  =  Expr |  Expr ; Program

Expr  =  Arithm |  Arithm :: Expr

Arithm  =  A1 |  A1 && Arithm |  A1 || Arithm

A1  =  A2 |  !A1

A2  =  A3 |  A3 == A3 |  A3 != A3
 |  A3 < A3 |  A3 > A3 |  A3 <= A3
 |  A3 >= A3 |  A3 =. A3 |  A3 !=. A3
 |  A3 <. A3 |  A3 >. A3 |  A3 <=. A3
 |  A3 >=. A3
A3  =  A4 |  A4 + A3 |  A4 - A3
 |  A4 +. A3 |  A4 -. A3
A4  =  A5 |  A5 * A4 |  A5 / A4
 |  A5 % A4 |  A5 *. A4 |  A5 /. A4
A5  =  A6 |  A6 & A5 |  A6 | A5
 |  A6 ^A5 |  A6 << A5 |  A6 >> A5
A6  =  Term |  -A6 |  ~A6
 |  -. A6 |  - int |  - float
 |  float

Term  =  ( Program )
 |  int |  'char’ |  nil
 |  string |  Xml
 |  [  NameOfField : Expr (NameOfField : Expr)* ]
 |  [Expr* ] |  {Expr* }


 |  Var(.Term)* |  set Var(.Term)* = Expr
 |  Var(.NameOfField)* |  set Var(.NameOfField)* = Expr

 |  Function  ArgsFunction |  #Function
 |  #{ Expr Expr Type}

 |    let Expr -> Locals in Expr
 |    if Expr then Expr else Expr
 |    while Expr do Expr
 |    for Local = Expr ; Expr ; Expr do Expr
 |    for Local = Expr ; Expr do Expr
 |    call Expr  Expr
 |    update Expr with [ {_ , Expr}* ]

 |  Constr  Expr |  Constr0 |  match Expr with Case

ArgsF  =  Expr ...Expr   : autant de fois Expr que la fonction F a d’arguments
Args  =  nothing |  Local Args
Locals  =  Local |  (Locals’::Locals)
Locals’  =  Local |  [ Locals’’ ]
Locals’’  =  {_ , Locals}*

Val  =  Val3 |  Val3 :: Val
Val3  =  Val4 |  Val4 + Val3 |  Val4 - Val3
 |  Val4 +. Val3 |  Val4 -. Val3
Val4  =  Val5 |  Val5 * Val4 |  Val5 / Val4
 |  Val5 % Val4 |  Val5 *. Val4 |  Val5 /. Val4
Val5  =  Val6 |  Val6 & Val5 |  Val6 | Val5
 |  Val6 ^Val5 |  Val6 << Val5 |  Val6 >> Val5
Val6  =  Val7 |  -Val6 |  ~Val6
 |  -. Val6 |  - int |  - float
 |  float
Val7  =  int |  'char’ |  nil
 |  string |  Xml  |  [ Val* ]
 |  (Val) |  { Val* }
 |  itof Val |  ftoi Val

Fields  =  Field |  Field, Fields
Field  =   NameOfField |    NameOfField : TypeMono

TypeConstr  =  TypeConstr’ |  TypeConstr’ | TypeConstr
TypeConstr’  =  Constr  TypeMono |  Constr0

Case  =  Case' |  Case' | Case |  ( _ -> Program)
Case'  =  ( Constr  Local -> Program ) |  (Constr0 -> Program )


Var  =  nom de variable
Function  =  nom de fonction
TypeName  =  nom de type |    nom de type( labels* )

Local  =  variable locale (liée)
NameOfField  =  nom de champ dans une structure
Constr  =  constructeur de type
Constr0  =  constructeur de type vide

Xml  =  < Tag (Attribute*)>Sub</ Tag >
Sub  =  nothing
  |  Text
  |  Xml Sub
  |  Text Xml Sub
Attribute  =  label = string

int  =  entier
char  =  caractère
string  =  chaîne
float  =  flottant

Les entiers peuvent être codés dans les bases suivantes:

  - en décimal - 12349
  - en hexa - 0x3fe
  - en binaire - 0b10011
  - en octal - 0o234235

Ils sont codés sur 31 bits signés.

Les char permettent de récupérer le code ascii d'un caractère: 'A est un entier qui vaut 65.

Les chaînes de caractères sont entre guillemets. Le caractère \ permet d'accéder à certaines commandes:

  \n - retour chariot
  \z -  caractère NULL
  \" -  guillemet
  \\ -  \
  \<nombre en décimal> - \132 est le caractère Ascii 132

Un \ en fin de ligne permet de signaler au compilateur de ne pas tenir compte du retour à la ligne.

Les remarques sont, comme en C, entre /*...*/ et peuvent être imbriquées les unes dans les autres.

## Fondamentaux du langage

### Hello world

On suppose l’existence d’une fonction Secholn de type ‘fun [S] S’, qui retourne l’argument, et qui, en effet de bord, affiche l’argument sur la sortie standard, suivi d’un retour à la ligne.

On suppose également qu’au démarrage, le système évalue la fonction main  de type ‘fun[]I’.

Dans ce cas l’exemple ‘Hello world’ s’écrit simplement:

    fun main=
      Secholn "hello world" ;
      0 ;;

Dans la suite on suppose l’existence des fonctions suivantes:

  - Secho de type ‘fun [S] S’, équivalente à Secholn, mais sans le retour à la ligne
  - Iecholn de type ‘fun [I] I’, équivalente à Secholn, mais pour les entiers
  - Iecho de type ‘fun [I] I’, équivalente à Secho, mais pour les entiers

### Calcul et variables
On peut définir une variable globale entière x initialisée avec la valeur ‘1’ de la manière suivante:
var x=1;;

Dans l’exemple suivant, on veut calculer x+y, et (x+y)², en utilisant le premier résultat pour calculer le second, ce qui nécessite de créer une variable locale contenant x+y.

    var x=123 ;;
    var y=456 ;;
    fun main=
      let x+y->z in
      (
        Secho "x+y=" ; Iecholn z ;
        Secho "(x+y)²=" ; Iecholn z*z
      ) ;
      0 ;;

L’opérateur `let ... -> ... in ...` permet de créer une variable locale dont le scope est uniquement l’expression qui suit le `in`.

On peut modifier une variable globale grâce à l’opérateur `set ...=...`  qui retourne la valeur passée en argument et qui, par effet de bord, remplace la valeur de la variable.

Dans l’exemple suivant, on veut calculer _x+y_ et placer le résultat dans _z_.

    var x=123 ;;
    var y=456 ;;
    var z ;;
    fun main=
      set z=x+y ;
      0 ;;

On remarque qu’il n’est pas nécessaire d’initialiser une variable globale. Dans ce cas sa valeur initiale est `nil`. `nil` est une valeur que peuvent prendre toutes variables, quelque soit leur type, et qui est équivalent à « vide ».

L’opérateur `if...then...[else ...]` est une fonction qui, en fonction du résultat de l’expression _condition_ calcule l’expression _then_ ou l’expression _else_. Le résultat de cette expression est le résultat de la fonction _if_. On peut donc intégrer cet opérateur dans une expression arithmétique:

    1+if x==2 then 3 else 4

###  Itération

Le langage propose un opérateur d’itération: `for ... ; ... [ ; ...] do ...`

Dans le cas d’une itération _simple_, on peut écrire par exemple: `for i=0 ; i<20 do ...`

L’expression qui suit le _do_ est alors évaluée 20 fois avec la variable locale _i_, créée pour l’occasion et dont le scope se limite à l’expression qui suit le _do_.

Si l’itération n’est pas de type _+1_ (par exemple on veut _+2_), on utilise la forme:

    for i=0 ; i<20 ; i+2 do ... // on écrit i+2, et non pas i=i+2, le ‘i=’ étant implicite

Le langage propose également un opérateur while: `while ... do ...`

Il n’y a toutefois pas de commande `break` ou `continue`.

###  Gestion de listes

Les langages fonctionnels se prêtent bien à gestion de listes.

La manipulation des listes s’appuie sur trois opérateurs:

  - ... :: ... (double deux-points): constructeur de liste `fun [u0 list u0] list u0`
  - `hd` - récupération du premier élément `fun [list u0] u0`
  - `tl` - récupération de la liste privée de son premier élément `fun [list u0] list u0`

La liste vide vaut ‘nil’

On écrit la fonction ‘dumpListI’ qui affiche le contenu d’une liste d’entiers.

    fun dumpListI l=
      if l==nil then Secholn "nil"
      else
      (
        Iecho hd l; Secho "::";
        dumpListI tl l
      );;

On peut également écrire:

    fun dumpListI l0=
      for l=l0;l!=nil;tl l do (Iecho hd l; Secho "::");
      Secholn "nil";;


Pour concaténer deux listes quelconques:

    fun conc p q= if p==nil then q else (hd p) ::conc tl p q ;;


###  Gestion des tuples
Un tuple est un ensemble de valeurs quelconques ; on le note entre crochets, par exemple:

    [123 “abc”]

On crée un tuple implicitement par cette écriture. On accède aux éléments d’un tuple par l’opérateur `let`:

    let tuple->[a b] in ...

Par exemple, on peut utiliser les tuples pour définir des vecteurs à 2 dimensions:

    fun tup2_add a b=
      let a->[xa ya] in
      let b->[xb yb] in
      [xa+xb ya+yb];;

On peut modifier une ou plusieurs valeurs d’un tuple par l’opérateur ‘update’:

    let [123 "abc"] -> t in
    (
      update t with [456 "def"] ;
      update t with [_ "def"];
      t
    );

On évitera toutefois cet usage. Si on doit effectuer des effets de bord sur les tuples, on préfera utiliser des structures (voir plus loin).

###  Gestion des tables
Une table est un ensemble de valeurs de même type ; on la note entre accolades, par exemple:

    {1 2 3}

On peut créer une table de deux manières:

  - en utilisant l’accolade
  - en utilisant l’opérateur tabnew: `fun[u0 I] tab u0`

L’opérateur tabnew prend en argument la valeur d’initialisation des éléments de la table ainsi que la taille d’un table.

On accède à un élément de la table de la manière suivante:

   t.i //i-ème élément de la table t

Comme en C, les éléments sont numérotés à partir de zéro. Si la valeur ‘i’ est hors limite (négative ou supérieure à la taille du tableau), la valeur retournée est ‘nil’.

On peut changer la valeur d’un élément de la table avec l’opérateur set:

    set t.i = t.i + 1 ;

###  Gestion des structures
Une structure est une sorte de tuple dont les champs sont nommés, ce qui permet d’y accéder plus simplement.

On doit d’abord définir les champs de la structure:

    type AAA=[nameAAA scoreAAA] ;;

On peut alors créer une structure en écrivant:

    [nameAAA : "foobar"  scoreAAA : 123]

On pourra accéder aux champs de la manière suivante:

    Secholn s.nameAAA ;

On peut changer la valeur d’un élément de la structure par l’opérateur set:

    set s.scoreAAA = 1 + s.scoreAAA ;


###  Gestion des types somme
Les types somme sont l’équivalent du ‘union’ du langage C. On peut l’utiliser pour implémenter les automates, ou certains arbres d’évaluation.

On définit par exemple les différents états des noeuds d’un arbre:
    type MySum= Zero | Const _ | Add _ | Mul _ ;;

Le caractère ‘undescore’ indique qu’un paramètre est associé au type somme.

    fun eval z=
      match z with
      ( Zero -> 0)
      ( Const a -> a)
     |( Add [x y] -> (eval x)+(eval y))
     |( Mul [x y] -> (eval x)*(eval y)) ;;

On crée alors l’arbre de l’expression `1 + (2 * 3)` de la manière suivante:

     Add [ Const 1 Mul [ Const 2 Const 3]]

###  Manipulation de fonctions
Le langage permet la manipulation de fonctions ; pour obtenir une sorte de « pointeur » vers une fonction, on utilise l’opérateur #. On utilise alors l’opérateur « call » pour appeler une fonction à partir de son pointeur.

    fun compare x y= x-y ;;

    fun main =
      compare 1 2 ;
      let #compare -> f in
      Iecholn call f [1 2] ;;  //call: fun[ fun u0 u1 u0]u1

On peut fixer le dernier argument d’une fonction et obtenir ainsi une fonction avec un argument de moins.

    fun main=
      let #compare -> f in
      let fixarg2 f 2 -> g in
      Iecholn call g [1] ;;

Dans cet exemple, la fonction g est la fonction de comparaison avec l’entier ‘2’.
On utilise l’opérateur fixargn, avec n=1, 2, 3, ...



## Exemples simples
###  Génération d’une liste d’entiers aléatoires

On suppose l’existence d’une fonction ‘rand’ qui retourne un nombre aléatoire sur 16 bits.

    fun mkrandomlist n=
      if n>0 then rand ::mkrandomlist n-1 ;;

###  Tri par insertion d’une liste d’entiers

    fun insert x l=
      if l==nil then x::nil
      else if (x-hd l )>0 then (hd l)::insert x tl l
      else x::l;;

    fun sort l= if l!=nil then insert hd l sort tl l;;



## Commands
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
| core | strfindrev | fun[S I S I I]I | recherche inversée d'une sous-chaîne dans une chaîne : chaîne, index, sous-chaîne, index, taille ; retourne l'index dans la chaîne (nil si non trouvé) |
| core | strlen | fun[S]I | taille d'une chaîne de caractères |
| core | strget | fun[S I]I | retourne le n-ième caractères d'une chaîne (entier entre 0 et 255) |
| core | strsub | fun[S I I]S | calcul d'une sous-chaîne : chaîne source, index source, taille |
| core | strcat | fun[S S]S | concaténation de deux chaînes |
| core | strcatlist | fun[list S]S | concaténation d'une liste de chaînes |
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

## bytecode

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
