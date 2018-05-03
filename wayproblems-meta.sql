
drop table if exists meta;

PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE meta ( key varchar, value varchar );
INSERT INTO "meta" VALUES('contact.email','f@zz.de');
INSERT INTO "meta" VALUES('contact.name','Florian Lohoff');
INSERT INTO "meta" VALUES('layer.wayproblems.geometrycolumn','geometry');
INSERT INTO "meta" VALUES('layer.wayproblems.srid','4326');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.color','#ff0000');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.stroke-width','4');
INSERT INTO "meta" VALUES('layer.wayproblems.styles.default.stroke-opacity','0.9');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:0','id');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:1','key');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:2','value');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:3','changeset');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:4','user');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:5','timestamp');
INSERT INTO "meta" VALUES('layer.wayproblems.columns:6','problem');
INSERT INTO "meta" VALUES('layer.wayproblems.popup', ( readfile('wayproblems-meta.popup' )) );

INSERT INTO "meta" VALUES('layer.wayproblems.shortdescription', 'Way problems for owl');
INSERT INTO "meta" VALUES('layer.wayproblems.boundary', ( readfile("owl.geojson") ));

INSERT INTO "meta" VALUES ( 'time.data', '2018-05-03T12:35:02Z');
INSERT INTO "meta" VALUES ( 'time.process', '2018-05-03T12:35:02Z');

COMMIT;
