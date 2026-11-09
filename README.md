# OctoLog

Gestion du matériel et des emprunts du club de plongée Sub Alcatel.
Application de bureau Qt 6 (Widgets), apparence native Windows, base SQLite locale.

## Fonctionnalités

- **Ouverture sur l'onglet Blocs** (emprunt de bloc = action principale).
- **Accueil** (tableau de bord) : chiffres clés, graphiques (disponibilité du matériel par type, emprunts par mois
  sur 12 mois, état de l'entretien obligatoire ; info-bulle au survol, clic pour ouvrir l'onglet), et listes courtes
  (6 lignes) des emprunts les plus anciens et des entretiens à prévoir, avec lien vers le détail.
- **Onglets** : Accueil, Adhérents, Blocs, Détendeurs, Gilets, Emprunts en cours.
  Les listes n'affichent que l'essentiel (nom, téléphone, droits de prêt… / capacité, taille, disponibilité, emprunteur) ;
  toutes les caractéristiques restent cherchables.
- **Fiche détaillée** (clic droit ou bouton *Fiche détaillée…*) : toutes les caractéristiques, emprunts en cours et
  **historique des emprunts** (clic droit pour supprimer une entrée erronée).
- **Prêt dans les deux sens** :
  - depuis un adhérent : *Emprunter du matériel…* → choix du matériel disponible (plusieurs éléments, tous types confondus) ;
  - depuis un matériel : *Prêter à un adhérent…* → choix de l'emprunteur (filtré par défaut sur les adhérents ayant le droit de prêt correspondant).
  - retour depuis l'adhérent, le matériel ou l'onglet *Emprunts en cours*.
- **Entretien du matériel** (onglet *Entretien*, bouton *Entretien* des onglets matériel, fiche détaillée) :
  - un entretien a une **personne responsable**, une date de début, puis à la fin les **actions effectuées**,
    un résultat (conforme / non conforme) et un **coût** facultatif ;
  - **un matériel en cours d'entretien ne peut pas être emprunté** (et un matériel emprunté doit être rendu
    avant d'entrer en entretien) ; *Enregistrer un entretien effectué* saisit directement un entretien terminé
    (ex. séance TIV pour plusieurs blocs) ;
  - échéances calculées automatiquement :

    | Matériel | Obligation | Périodicité |
    |---|---|---|
    | Blocs | Inspection périodique (TIV) | 12 mois maximum |
    | Blocs — régime dérogatoire TIV (défaut, bouteilles métalliques) | Requalification | 6 ans |
    | Blocs — régime standard (et tout bloc composite) | Requalification | 2 ans |
    | Détendeurs | Révision | 12 mois (règle du club) |
    | Gilets et autres | aucune | entretiens enregistrés sans échéance |

    Référence : arrêté du 20 novembre 2017 relatif au suivi en service des équipements sous pression
    (en vigueur au 1er janvier 2018 ; requalification TIV passée de 5 à 6 ans) et cahier des charges TIV de la FFESSM.
    Le régime et le matériau de chaque bloc se règlent dans sa fiche (*Modifier…*).
  - états : *À jour*, *À prévoir* (échéance dans 30 jours), *En retard* ; colonne *Entretien* dans les listes,
    filtres dédiés, liste « Entretien à prévoir » sur l'Accueil, avertissement au prêt d'un matériel dont
    l'entretien obligatoire est dépassé ;
  - journal des entretiens avec coût total des lignes affichées ; une fin d'entretien non conforme propose de
    mettre le matériel hors service.
- **Cautions des prêts payants** (onglet *Cautions*, bouton *Caution…* de l'adhérent) :
  - un prêt est payant quand le droit de prêt vaut « Oui » (un « Oui (Encadrant) » est gratuit) ;
  - une caution par adhérent et par saison (septembre → août) couvre tous ses prêts payants :
    montant, moyen (chèque, espèces, virement), n° de chèque, banque, date de réception ;
  - clôture individuelle ou en lot : *Restituée*, *Détruite* ou *Encaissée* ;
  - listes : cautions détenues, **à restituer / détruire** (l'adhérent a tout rendu), clôturées,
    et **cautions manquantes** (prêt payant sans caution) ; total des montants affichés ;
  - au prêt de matériel payant sans caution : *Enregistrer la caution…*, *Prêter sans caution* ou *Annuler* ;
  - colonne et filtre « Caution » dans la liste des adhérents, feuille *Cautions* dans l'extraction Excel.
- **Mode utilisateur / mode admin** :
  - **utilisateur** (au démarrage) : consulter, rechercher, exporter, **prêter et rendre** ;
  - **admin** (bouton *Mode admin* ou menu *Administration*, **code PIN** de 4 à 6 chiffres) : fiches adhérents
    et matériel, entretiens, cautions, import Excel, changement de base, suppressions d'historique ;
  - le code est défini au premier passage en admin, stocké chiffré dans la base (il suit le fichier),
    modifiable par *Administration › Changer le code PIN* ;
  - en mode utilisateur, les **données admin sont absentes** (pas seulement masquées) : pas d'onglets
    *Entretien* ni *Cautions*, pas de colonnes ni d'informations d'entretien ou de caution dans les listes,
    les fiches, l'accueil, la recherche et l'export (l'état « En entretien » reste visible pour expliquer
    qu'un matériel ne peut pas être prêté) ;
  - retour automatique en mode utilisateur après 10 minutes sans activité (ou Ctrl+L) ;
  - ligne de commande : `--admin` démarre en mode admin **sans code**, `--reset-pin` efface le code
    (redéfini au prochain passage en admin), `--set-pin 1234` définit le code.
- **Contrôles** : droits de prêt (bloc / détendeur / gilet), adhésion expirée, matériel hors service, matériel déjà emprunté (proposition de transfert).
- **Double-clic** : sur un adhérent → fenêtre d'emprunt ; sur un matériel → prêt (ou retour s'il est emprunté) ;
  sur un emprunt en cours → retour ; sur un emprunt dans une fiche → fiche de l'adhérent ou du matériel lié.
- **Clic droit** sur toutes les listes (adhérents, matériel, emprunts) : emprunter/prêter, rendre (un par un ou tout),
  fiche détaillée, fiche de l'emprunteur ou du matériel emprunté, modifier, nouveau, supprimer.
- **Recherche sur toutes les caractéristiques** (y compris les colonnes masquées) :

  | Saisie | Effet |
  |---|---|
  | `aqualung 15l` | lignes contenant les deux mots |
  | `"nage avec palmes"` | expression exacte |
  | `marque:roth` | la colonne *Marque…* contient « roth » |
  | `taille=m` / `email=` | égalité / colonne vide |
  | `capacité>=15` | comparaison numérique |
  | `inspection<2025`, `emprunt>=01/09/2026` | comparaison de dates (aaaa, mm/aaaa, jj/mm/aaaa) |
  | `-hors` | exclusion |

  Casse et accents ignorés, noms de colonnes abrégeables. Liste déroulante d'état : disponibles, empruntés, droits de prêt, adhésion expirée…
- **Colonnes** : clic droit sur l'en-tête pour afficher/masquer ; disposition mémorisée.
- **Import Excel** (*Fichier › Importer*, Ctrl+I) : sélectionner les classeurs (adhérents, blocs, détendeurs, gilets, emprunts actuels, historique). Le type est reconnu par les en-têtes.
  **Pour les adhérents et le matériel, le fichier fait foi** : fiches créées ou mises à jour (une case vide vide la valeur).
  Un **adhérent absent** du fichier est **désactivé** (fiche et historique conservés, masqué par le filtre « Actifs »,
  avertissement s'il emprunte) et réactivé s'il réapparaît dans un import suivant.
  Un **matériel absent** est supprimé, sauf s'il est emprunté (conservé et signalé).
  Un récapitulatif (ajouts, modifications, suppressions) est présenté avant application ; *Annuler* ne change rien.
  L'historique des fiches supprimées est conservé et rattaché automatiquement si la fiche est réimportée.
  Les emprunts importés s'ajoutent sans doublon ; un emprunteur inconnu est créé automatiquement (désactivé).
- **Export / extraction** (barre d'outils *Données* ou menu *Fichier*) :
  - *Exporter la vue* (Ctrl+E) : l'onglet affiché, tel qu'il est filtré, en `.xlsx` ou `.csv` ;
  - *Exporter tout* (Ctrl+Maj+E) : extraction complète dans un classeur Excel, une feuille par sujet —
    Adhérents, Blocs, Détendeurs, Gilets (toutes les caractéristiques, disponibilité, état d'entretien,
    prochaine échéance), Emprunts en cours, Historique des emprunts, Entretiens (responsable, actions, coût),
    Échéances. Dates et nombres sont de vraies valeurs Excel (tri, filtres, formules).
  - en ligne de commande : `OctoLog --export-all extraction.xlsx` (sans ouvrir la fenêtre).
- **Base** : fichier unique `octolog.octolog` dans `%APPDATA%\SubAlcatel\OctoLog` ; *Fichier › Ouvrir ou créer une base* pour en utiliser une autre (clé USB, dossier partagé), *Sauvegarder une copie* pour l'archivage.

## Compilation sous Windows

1. Installer Qt 6 (≥ 6.5) avec l'installeur en ligne Qt : composant *MinGW* (ou MSVC) + *Qt 6.x › MinGW*
   (le module *Qt SVG*, inclus de base, sert aux icônes), ainsi que CMake et Ninja (proposés dans *Developer and Designer Tools*).
2. Ouvrir `CMakeLists.txt` dans Qt Creator et compiler en *Release*,
   ou en ligne de commande depuis l'invite « Qt 6.x (MinGW) » :

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOCTOLOG_TESTS=OFF
```

```bash
cmake --build build
```

3. Rendre l'exécutable autonome (copie des DLL Qt, du pilote SQLite et des traductions) :

```bash
windeployqt --release build/OctoLog.exe
```

Le dossier `build` contient alors tout le nécessaire ; il peut être zippé et copié sur le PC du club.

> OctoLog utilise `QZipReader`/`QZipWriter` (en-têtes privés de QtCore) pour lire et écrire les `.xlsx`.
> Ils sont fournis par l'installeur Qt ; l'exécutable doit être déployé avec les DLL Qt de la même version (ce que fait `windeployqt`).

## Compilation sous Linux

```bash
cmake -S . -B build -G Ninja && cmake --build build && ./build/OctoLog
```

Tests : `ctest --test-dir build`.

Options de ligne de commande : `--db <fichier>` (base à ouvrir), `--import <fichier ou dossier>` (import au démarrage),
`--export-all <fichier.xlsx>` (extraction complète sans ouvrir la fenêtre), `--reset-pin` (efface le code PIN admin).
